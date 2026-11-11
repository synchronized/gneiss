// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_ENGINE_LOGGING_DETAIL_ASYNC_QUEUE_HPP
#define GNEISS_ENGINE_LOGGING_DETAIL_ASYNC_QUEUE_HPP

#include "engine/core/logging/record.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace gneiss::engine::logging::detail {

/** 单消费线程串行回调；销毁前排空队列，禁止在回调内 flush 或销毁投递器。 */
class async_queue final {
public:
  explicit async_queue(record_callback callback, std::size_t capacity = 1024U);
  ~async_queue() noexcept;

  async_queue(const async_queue&) = delete;
  async_queue& operator=(const async_queue&) = delete;

  /** 内部可信记录入口；只复制并投递，文本与级别合法性由调用边界保证。 */
  [[nodiscard]] engine::error submit(const message_view& message);
  void flush() noexcept;

private:
  struct owned_record final {
    std::uint64_t context{};
    level severity = level::info;
    std::uint64_t sequence = 0U;
    std::uint64_t timestamp_ns = 0U;
    std::uint64_t thread_id = 0U;
    std::string source;
    std::string category;
    std::string message;
    engine::error result = engine::error::success;
  };

  void run() noexcept;
  void deliver(const owned_record& event);
  [[nodiscard]] owned_record make_drop_event(std::uint64_t count);

  record_callback callback_;
  std::size_t capacity_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::condition_variable drained_;
  std::deque<owned_record> queue_;
  std::thread worker_;
  std::uint64_t next_sequence_ = 1U;
  std::uint64_t dropped_count_ = 0U;
  bool is_delivering_ = false;
  bool is_stopping_ = false;
};

} // namespace gneiss::engine::logging

#endif
