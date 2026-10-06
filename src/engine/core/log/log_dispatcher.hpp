// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_LOG_LOG_DISPATCHER_HPP_
#define GNEISS_SRC_LOG_LOG_DISPATCHER_HPP_

#include <gneiss/log.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace gneiss::log_internal {

/** 提交期间借用文本；context 由调用方解释，零表示无关联上下文。 */
struct message_view final {
  std::uint64_t context{};
  std::uint32_t severity{GNEISS_LOG_INFO};
  std::string_view source{};
  std::string_view category{};
  std::string_view message{};
  gneiss_result result{GNEISS_SUCCESS};
};

/** 所有文本只在回调期间有效，接收方不得保留视图。 */
struct event_view final {
  message_view message;
  std::uint64_t sequence{};
  std::uint64_t timestamp_ns{};
  std::uint64_t thread_id{};
};
using event_sink = std::function<void(const event_view&)>;

/** 单消费线程串行回调；销毁前排空队列，禁止在回调内 flush 或销毁投递器。 */
class log_dispatcher final {
public:
  explicit log_dispatcher(event_sink callback, std::size_t capacity = 1024U);
  ~log_dispatcher() noexcept;

  log_dispatcher(const log_dispatcher&) = delete;
  log_dispatcher& operator=(const log_dispatcher&) = delete;

  [[nodiscard]] gneiss_result submit(const message_view& message) noexcept;
  void flush() noexcept;

private:
  struct owned_event final {
    std::uint64_t context{};
    std::uint32_t severity = GNEISS_LOG_INFO;
    std::uint64_t sequence = 0U;
    std::uint64_t timestamp_ns = 0U;
    std::uint64_t thread_id = 0U;
    std::string source;
    std::string category;
    std::string message;
    gneiss_result result = GNEISS_SUCCESS;
  };

  void run() noexcept;
  void deliver(const owned_event& event) noexcept;
  [[nodiscard]] owned_event make_drop_event(std::uint64_t count) noexcept;

  event_sink callback_;
  std::size_t capacity_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::condition_variable drained_;
  std::deque<owned_event> queue_;
  std::thread worker_;
  std::uint64_t next_sequence_ = 1U;
  std::uint64_t dropped_count_ = 0U;
  bool is_delivering_ = false;
  bool is_stopping_ = false;
};

} // namespace gneiss::log_internal

#endif
