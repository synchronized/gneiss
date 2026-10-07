// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/progress_notification.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace gneiss::core {

/** 原生宿主的有限等待；代次在处理队列前读取，避免检查与入睡之间丢通知。
 * 不在 Web/无线程宿主实例化；超时是请求期限，不是系统调度的实时保证。 */
class loop_progress final : public progress_notification {
public:
  [[nodiscard]] std::uint64_t snapshot() const noexcept {
    return generation_.load(std::memory_order_acquire);
  }
  void notify() noexcept override {
    {
      const std::scoped_lock lock(mutex_);
      generation_.fetch_add(1U, std::memory_order_release);
    }
    ready_.notify_one();
  }
  bool wait(std::uint64_t observed, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return ready_.wait_for(lock, timeout, [&] { return snapshot() != observed; });
  }

private:
  std::atomic_uint64_t generation_{};
  std::mutex mutex_;
  std::condition_variable ready_;
};

} // namespace gneiss::core
