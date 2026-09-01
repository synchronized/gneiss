// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SRC_LOG_LOG_DISPATCHER_H_
#define GNEISS_SRC_LOG_LOG_DISPATCHER_H_

#include <gneiss/application.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace gneiss::log_internal {

class log_dispatcher final {
public:
  log_dispatcher(gneiss_application_log_fn callback, void* user_data, std::size_t capacity = 1024U);
  ~log_dispatcher() noexcept;

  log_dispatcher(const log_dispatcher&) = delete;
  log_dispatcher& operator=(const log_dispatcher&) = delete;

  [[nodiscard]] gneiss_result submit(gneiss_application application,
                                     const gneiss_log_message& message,
                                     std::string_view source) noexcept;
  void flush() noexcept;

private:
  struct owned_event final {
    gneiss_application application = GNEISS_NULL_APPLICATION;
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

  gneiss_application_log_fn callback_;
  void* user_data_;
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
