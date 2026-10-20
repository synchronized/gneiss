// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "uv_async_file.hpp"
#include <caors/io/async_file.h>
#include <mutex>
#include <vector>

namespace gneiss::caors_probe {
inline caors::io::io_error translate(result status) {
  using caors::io::io_errc;

  if (status == result::success)
    return {};
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

// 验证阶段由宿主 pump 推进；所有完成凭证在锁外交付，允许内联 completion。
class async_file final : public caors::io::async_file {
  struct read {
    std::unique_ptr<io_internal::uv_file_read_operation> operation;
    caors::memory_resource* resource{};
    caors::io::io_completion<caors::io::byte_buffer> completion;
  };

public:
  explicit async_file(std::shared_ptr<io_internal::uv_async_file> file) : file_(std::move(file)) {}
  ~async_file() override { file_->begin_close(); }
  // 宿主放弃 pump 前必须调用；解除凭证保活环，真实 I/O 仍由 libuv 排空。
  void abandon() {
    std::vector<read> reads;
    std::vector<caors::io::io_completion<caors::io::close_result>> closes;
    {
      const std::scoped_lock lock(mutex_);
      closing_ = true;
      reads.swap(reads_);
      closes.swap(closes_);
      file_->begin_close();
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
    // 先保留容器容量，保证底层接受后不再出现入队分配失败。
    reads_.reserve(reads_.size() + 1);
    read pending;
    const auto status =
        file_->begin_read(offset, static_cast<std::size_t>(length), pending.operation);
    if (status != result::success)
      return translate(status);
    pending.resource = resource;
    pending.completion = std::move(completion);
    reads_.push_back(std::move(pending));
    return {};
  }
  caors::io::io_error
  start_close(caors::io::io_completion<caors::io::close_result> completion) override {
    const std::scoped_lock lock(mutex_);
    closes_.push_back(std::move(completion));
    closing_ = true;
    file_->begin_close();
    return {};
  }
  void pump() {
    std::vector<read> ready;
    std::vector<caors::io::io_completion<caors::io::close_result>> closed;
    result close_status = result::not_ready;
    {
      const std::scoped_lock lock(mutex_);
      ready.reserve(reads_.size());
      for (auto it = reads_.begin(); it != reads_.end();) {
        std::span<const std::byte> bytes;
        if (it->operation->poll(bytes) == result::not_ready) {
          ++it;
          continue;
        }
        ready.push_back(std::move(*it));
        it = reads_.erase(it);
      }
      close_status = file_->poll_close();
      if (closing_ && close_status != result::not_ready && reads_.empty())
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
  std::shared_ptr<io_internal::uv_async_file> file_;
  std::mutex mutex_;
  bool closing_{};
  std::vector<read> reads_;
  std::vector<caors::io::io_completion<caors::io::close_result>> closes_;
};
} // namespace gneiss::caors_probe
