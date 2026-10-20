// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "async_file.hpp"
#include <caors/io/native_file_system.h>

namespace gneiss::caors_probe {
// 验证用本地叶后端：空根路径，不另建挂载表。宿主借用的 io/loop 必须最后销毁。
// start_open 可由 worker 调用；pump/abandon 由同一宿主线程串行、非重入调用。
class file_system final : public caors::io::file_system {
  struct opening {
    std::unique_ptr<io_internal::uv_file_open_operation> operation;
    caors::io::io_completion<std::shared_ptr<caors::io::async_file>> completion;
  };

public:
  explicit file_system(io_internal::uv_file_io& io) : io_(io) {}
  bool exists(caors::io::path_view path) const override { return native_.exists(path); }
  caors::io::byte_buffer_result read_all(caors::io::path_view path,
                                         caors::memory_resource* resource) override {
    return native_.read_all(path, resource);
  }
  caors::error write_all(caors::io::path_view, std::span<const std::byte>) override {
    return {caors::error_domain::io, caors::errc::unsupported};
  }
  caors::io::file_system_capabilities capabilities() const noexcept override {
    return {.read = true, .seek = true};
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
    const auto status = io_.begin_open(location.path.string_view(), next.operation);
    if (status != result::success)
      return translate(status);
    next.completion = std::move(completion);
    pending_.push_back(std::move(next));
    return {};
  }
  void pump() {
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
      std::shared_ptr<io_internal::uv_async_file> source;
      const auto status = next.operation->poll(source);
      if (status == result::not_ready) {
        const std::scoped_lock lock(mutex_);
        pending_.push_back(std::move(next));
        continue;
      }
      if (status != result::success) {
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
  void abandon() {
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
  io_internal::uv_file_io& io_;
  caors::io::native_file_system native_;
  std::mutex mutex_;
  bool stopped_{};
  std::vector<opening> pending_;
  std::vector<std::weak_ptr<async_file>> files_;
};
} // namespace gneiss::caors_probe
