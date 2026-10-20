// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "adapter.hpp"
#include <atomic>
#include <caors/assets.h>
#include <caors/io.h>
#include <functional>
#include <gneiss/engine/core/result.hpp>
#include <mutex>

namespace gneiss::caors_internal {
/** 有界 CPU 文档加载协议；宿主串行 pump，销毁前停止 worker 再 abandon。
 * decode/verify 在 worker 执行；平台层不理解资源内容，不持有资源缓存。 */
template <typename Asset> class document_loader : public caors::resources::async_loader<Asset> {
public:
  virtual void pump() = 0;
  virtual bool empty() = 0;
  virtual void abandon() = 0;
  virtual std::size_t started_count() const noexcept = 0;
};
using caors::errc;
using caors::error;
using caors::error_domain;
template <typename Asset> class bounded_document_loader final : public document_loader<Asset> {
  struct operation {
    std::string uri;
    caors::resources::async_load_context context;
    caors::resources::load_completion<Asset> completion;
    caors::io::io_request<std::shared_ptr<caors::io::async_file>> opening;
    std::shared_ptr<caors::io::async_file> file;
    caors::io::io_request<caors::io::byte_buffer> reading;
  };

public:
  bounded_document_loader(
      gneiss::caors_internal::file_system_adapter& files,
      std::function<result(std::span<const std::byte>, Asset&)> decode,
      std::function<result(std::string_view, std::span<const std::byte>)> verify)
      : decode_(std::move(decode)), verify_(std::move(verify)), files_(files) {}
  error start(caors::resources::async_load_context context,
              caors::resources::load_completion<Asset> completion) override {
    if (!context.sources)
      return failure(errc::invalid_argument);
    auto resolved = context.sources->resolve(context.guid);
    if (!resolved)
      return resolved.error();
    // provider 固定来源快照；路径语义由具体后端负责，可为完整资产 URI。
    auto op = std::make_shared<operation>();
    op->uri = resolved.value.path.string_view();
    op->context = std::move(context);
    op->completion = std::move(completion);
    auto request =
        caors::io::request_open(std::move(resolved.value), io_completions_.get_executor());
    if (!request) {
      (void)op->completion.complete({translate(request.error()), {}});
      return {};
    }
    op->opening = std::move(request.value);
    const std::scoped_lock lock(mutex_);
    pending_.push_back(std::move(op));
    ++starts_;
    return {};
  }
  void pump() override {
    files_.pump();
    (void)io_completions_.drain();
    const std::scoped_lock lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
      const auto& op = *it;
      if (op->context.stop.stop_requested()) {
        (void)op->completion.complete({failure(errc::cancelled), {}});
        it = pending_.erase(it); // libuv 在途对象自行保活，宿主 loop 最后排空。
        continue;
      }
      result status = result::success;
      error io_error;
      if (op->opening) {
        auto opened = op->opening.result();
        if (!opened) {
          ++it;
          continue;
        }
        status = *opened ? result::success : result::io;
        if (!*opened)
          io_error = translate(opened->error());
        op->opening = {};
        if (status == result::success) {
          op->file = opened->value;
          auto request =
              caors::io::read_all_bounded(op->file, max_bytes, io_completions_.get_executor());
          if (!request) {
            status = result::io;
            io_error = translate(request.error());
          } else
            op->reading = std::move(request.value);
        }
      }
      std::shared_ptr<const caors::io::io_result<caors::io::byte_buffer>> read_result;
      std::span<const std::byte> bytes;
      if (status == result::success) {
        (void)io_completions_.drain();
        read_result = op->reading.result();
        status = !read_result ? result::not_ready : (*read_result ? result::success : result::io);
        if (read_result && !*read_result)
          io_error = translate(read_result->error());
        if (status == result::success)
          bytes = {read_result->value.data(), read_result->value.size()};
      }
      if (status == result::not_ready) {
        ++it;
        continue;
      }
      if (status != result::success) {
        (void)op->completion.complete({io_error, {}});
      } else {
        // 捕获回执保持借用字节有效；后台解析完成后才释放文件和缓冲。
        auto owned = op;
        const auto posted = op->context.worker.post(
            caors::tasks::task{[owned, bytes, decode = decode_, verify = verify_] {
              if (owned->context.stop.stop_requested()) {
                (void)owned->completion.complete({failure(errc::cancelled), {}});
                return;
              }
              try {
                auto value =
                    caors::memory::make_unique<Asset>(owned->context.resource, owned->context.guid);
                const auto parsed = decode(bytes, *value);
                if (parsed != result::success) {
                  (void)owned->completion.complete(
                      {failure(parsed == result::out_of_memory ? errc::out_of_memory
                                                               : errc::invalid_argument),
                       {}});
                  return;
                }
                // 发布缓存前在工作线程复验；禁止主线程执行文件复读。
                const auto verified = verify ? verify(owned->uri, bytes) : result::success;
                if (verified != result::success) {
                  (void)owned->completion.complete({failure(errc::backend_failure), {}});
                  return;
                }
                if (owned->context.stop.stop_requested()) {
                  (void)owned->completion.complete({failure(errc::cancelled), {}});
                  return;
                }
                (void)owned->completion.complete({{}, std::move(value)});
              } catch (const std::bad_alloc&) {
                (void)owned->completion.complete({failure(errc::out_of_memory), {}});
              } catch (...) {
                (void)owned->completion.complete({failure(errc::backend_failure), {}});
              }
            }});
        if (posted)
          (void)op->completion.complete({posted, {}});
      }
      it = pending_.erase(it);
    }
  }
  bool empty() override {
    const std::scoped_lock lock(mutex_);
    return pending_.empty();
  }
  // 调度器已排空后由宿主调用；回执释放不等待真实 I/O。
  void abandon() override {
    files_.abandon();
    pending_.clear();
    (void)io_completions_.drain();
  }
  std::size_t started_count() const noexcept override { return starts_.load(); }
  std::atomic_size_t starts_{};
  static constexpr std::size_t max_bytes = 1024U * 1024U;

private:
  static error translate(caors::io::io_error error) {
    using caors::io::io_errc;
    if (error.value == io_errc::not_found)
      return failure(errc::not_found);
    if (error.value == io_errc::out_of_memory)
      return failure(errc::out_of_memory);
    if (error.value == io_errc::cancelled)
      return failure(errc::cancelled);
    if (error.value == io_errc::unsupported)
      return failure(errc::unsupported);
    if (error.value == io_errc::invalid_argument || error.value == io_errc::limit_exceeded ||
        error.value == io_errc::out_of_range)
      return failure(errc::invalid_argument);
    return failure(errc::backend_failure);
  }
  static error failure(errc value) { return {error_domain::resources, value}; }
  std::function<result(std::span<const std::byte>, Asset&)> decode_;
  std::function<result(std::string_view, std::span<const std::byte>)> verify_;
  caors::tasks::execution_context io_completions_;
  gneiss::caors_internal::file_system_adapter& files_;
  std::mutex mutex_;
  std::vector<std::shared_ptr<operation>> pending_;
};

} // namespace gneiss::caors_internal
