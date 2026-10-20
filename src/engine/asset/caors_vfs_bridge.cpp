// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "caors_vfs_bridge.hpp"
#include <gneiss/engine/core/result.hpp>
#include <mutex>
#include <vector>
namespace gneiss::asset_internal {
namespace {
inline caors::io::io_error translate(gneiss_result code) {
  const result status{code};
  using caors::io::io_errc;

  if (status == result::success)
    return {};
  if (status == result::unsupported)
    return {io_errc::unsupported};
  if (status == result::not_found)
    return {io_errc::not_found};
  if (status == result::invalid_state)
    return {io_errc::closed};
  if (status == result::invalid_argument)
    return {io_errc::invalid_argument};
  if (status == result::out_of_memory)
    return {io_errc::out_of_memory};
  if (status == result::not_ready)
    return {io_errc::pending};

  return {io_errc::backend_failure, 1, static_cast<gneiss_result>(status)};
}

class async_file final : public caors_internal::file_adapter {
  struct read {
    std::unique_ptr<read_operation> operation;
    caors::memory_resource* resource{};
    caors::io::io_completion<caors::io::byte_buffer> completion;
  };

public:
  explicit async_file(std::shared_ptr<read_source> file) : file_(std::move(file)) {}
  ~async_file() override { (void)file_->begin_close(); }
  // 宿主放弃 pump 前必须调用；解除凭证保活环，真实 I/O 仍由 libuv 排空。
  void abandon() override {
    std::vector<read> reads;
    std::vector<caors::io::io_completion<caors::io::close_result>> closes;
    {
      const std::scoped_lock lock(mutex_);
      closing_ = true;
      reads.swap(reads_);
      closes.swap(closes_);
      (void)file_->begin_close();
    }
  }
  std::uint64_t size() const noexcept override { return file_->size(); }
  std::uint64_t max_read_size() const noexcept override { return 1024U * 1024U; }
  caors::io::io_error
  start_read(std::uint64_t offset, std::uint64_t length, caors::memory_resource* resource,
             caors::io::io_completion<caors::io::byte_buffer> completion) override {
    using caors::io::io_errc;
    const std::scoped_lock lock(mutex_);
    if (closing_)
      return {io_errc::closed};
    if (offset > size() || length > size() - offset)
      return {io_errc::out_of_range};
    if (length > max_read_size())
      return {io_errc::limit_exceeded};
    if (offset != 0 || length != size())
      return {io_errc::unsupported}; // 首阶段仅桥接需要完整摘要的 CPU 文档。
    // 先保留容器容量，保证底层接受后不再出现入队分配失败。
    reads_.reserve(reads_.size() + 1);
    read pending;
    const auto status =
        file_->begin_read(offset, static_cast<std::size_t>(length), pending.operation);
    if (status != GNEISS_SUCCESS)
      return translate(status);
    pending.resource = resource;
    pending.completion = std::move(completion);
    reads_.push_back(std::move(pending));
    return {};
  }
  caors::io::io_error
  start_close(caors::io::io_completion<caors::io::close_result> completion) override {
    const std::scoped_lock lock(mutex_);
    closes_.reserve(closes_.size() + 1);
    const auto started = file_->begin_close();
    if (started != GNEISS_SUCCESS)
      return translate(started);
    closes_.push_back(std::move(completion));
    closing_ = true;
    return {};
  }
  void pump() override {
    std::vector<read> ready;
    std::vector<caors::io::io_completion<caors::io::close_result>> closed;
    gneiss_result close_status = GNEISS_ERROR_NOT_READY;
    {
      const std::scoped_lock lock(mutex_);
      ready.reserve(reads_.size());
      for (auto it = reads_.begin(); it != reads_.end();) {
        std::span<const std::byte> bytes;
        if (it->operation->poll(bytes) == GNEISS_ERROR_NOT_READY) {
          ++it;
          continue;
        }
        ready.push_back(std::move(*it));
        it = reads_.erase(it);
      }
      close_status = file_->poll_close();
      if (closing_ && close_status != GNEISS_ERROR_NOT_READY && reads_.empty())
        closed.swap(closes_);
    }
    for (auto& pending : ready) {
      using namespace caors::io;
      std::span<const std::byte> bytes;
      auto status = translate(pending.operation->poll(bytes));
      auto output = make_byte_buffer(pending.resource);
      if (pending.completion.stop_token().stop_requested())
        status = {io_errc::cancelled};
      if (!status) {
        try {
          const auto validated = file_->complete_validation(core::sha256(bytes));
          status = translate(validated);
          if (!status)
            output.assign(bytes.begin(), bytes.end());
        } catch (const std::bad_alloc&) {
          status = {io_errc::out_of_memory};
        }
      }
      (void)pending.completion.complete({status, std::move(output)});
    }
    for (auto& completion : closed)
      (void)completion.complete({translate(close_status), {}});
  }

private:
  std::shared_ptr<read_source> file_;
  std::mutex mutex_;
  bool closing_{};
  std::vector<read> reads_;
  std::vector<caors::io::io_completion<caors::io::close_result>> closes_;
};
class vfs_backend final : public caors_vfs_bridge {
  struct opening {
    std::unique_ptr<open_read_operation> operation;
    caors::io::io_completion<std::shared_ptr<caors::io::async_file>> completion;
  };

public:
  explicit vfs_backend(virtual_file_system files) : files_snapshot_(std::move(files)) {}
  bool exists(caors::io::path_view) const override { return false; } // 不提供同步状态查询。
  gneiss_result identify(std::string_view uri, std::uint32_t type,
                         asset_identity& output) const noexcept override {
    return files_snapshot_.identify(uri, type, output);
  }
  caors::io::byte_buffer_result read_all(caors::io::path_view, caors::memory_resource*) override {
    return {{caors::error_domain::io, caors::errc::unsupported}, {}};
  }
  caors::error write_all(caors::io::path_view, std::span<const std::byte>) override {
    return {caors::error_domain::io, caors::errc::unsupported};
  }
  caors::io::file_system_capabilities capabilities() const noexcept override {
    return {.read = true};
  }
  caors::io::io_error
  start_open(caors::io::resolved_file location, caors::memory_resource*,
             caors::io::io_completion<std::shared_ptr<caors::io::async_file>> completion) override {
    const std::scoped_lock lock(mutex_);
    if (stopped_)
      return {caors::io::io_errc::closed};
    if (location.backend.get() != this)
      return {caors::io::io_errc::invalid_argument};
    pending_.reserve(pending_.size() + 1);
    opening next;
    const auto status =
        files_snapshot_.begin_open_read_for_validation(location.path.string_view(), next.operation);
    if (status != GNEISS_SUCCESS)
      return translate(status);
    next.completion = std::move(completion);
    pending_.push_back(std::move(next));
    return {};
  }
  void pump() override {
    std::vector<opening> ready;
    {
      const std::scoped_lock lock(mutex_);
      // 单消费者打开回执不能预先 poll；先取出，再将未就绪项放回。
      ready.swap(pending_);
    }
    for (auto& next : ready) {
      if (next.completion.stop_token().stop_requested()) {
        (void)next.completion.complete({{caors::io::io_errc::cancelled}, {}});
        continue;
      }
      std::unique_ptr<read_source> source;
      const auto status = next.operation->poll(source);
      if (status == GNEISS_ERROR_NOT_READY) {
        const std::scoped_lock lock(mutex_);
        pending_.push_back(std::move(next));
        continue;
      }
      if (status != GNEISS_SUCCESS) {
        (void)next.completion.complete({translate(status), {}});
        continue;
      }
      try {
        auto file = std::make_shared<async_file>(std::move(source));
        files_.push_back(file);
        (void)next.completion.complete({{}, std::move(file)});
      } catch (const std::bad_alloc&) {
        (void)next.completion.complete({{caors::io::io_errc::out_of_memory}, {}});
      }
    }
    for (auto it = files_.begin(); it != files_.end();) {
      if (auto file = it->lock()) {
        file->pump();
        ++it;
      } else
        it = files_.erase(it);
    }
  }
  // 不再接收打开；释放所有未交付凭证，loop 继续排空真实操作。
  void abandon() override {
    std::vector<opening> pending;
    {
      const std::scoped_lock lock(mutex_);
      stopped_ = true;
      pending.swap(pending_);
    }
    for (auto& weak : files_)
      if (auto file = weak.lock())
        file->abandon();
    files_.clear();
  }

private:
  virtual_file_system files_snapshot_;
  std::mutex mutex_;
  bool stopped_{};
  std::vector<opening> pending_;
  std::vector<std::weak_ptr<caors_internal::file_adapter>> files_;
};
}
std::shared_ptr<caors_vfs_bridge> make_caors_vfs_bridge(virtual_file_system snapshot) {
  return std::make_shared<vfs_backend>(std::move(snapshot));
}
}
