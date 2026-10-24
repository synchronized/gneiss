// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "log_dispatcher.hpp"
#include "engine/core/result.hpp"
#include "log.hpp"

#include <chrono>
#include <functional>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

thread_local bool is_in_log_callback = false;

[[nodiscard]] std::uint64_t current_time_ns() noexcept {
  const auto time = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(time).count());
}

[[nodiscard]] std::uint64_t current_thread_id() noexcept {
  const auto value =
      static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  return value == 0U ? 1U : value;
}

} // namespace

namespace gneiss::engine::core::log {

log_dispatcher::log_dispatcher(event_sink callback, std::size_t capacity)
    : callback_(std::move(callback)), capacity_(capacity) {
  if (!callback_ || capacity_ == 0U) {
    throw std::invalid_argument("日志投递器配置无效");
  }
  worker_ = std::thread(&log_dispatcher::run, this);
}

log_dispatcher::~log_dispatcher() noexcept {
  {
    const std::scoped_lock lock(mutex_);
    is_stopping_ = true;
  }
  ready_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

gneiss::engine::core::result log_dispatcher::submit(const message_view& message) noexcept {
  if (is_in_log_callback) {
    return gneiss::engine::core::result::invalid_state;
  }
  if (message.severity < level::trace || message.severity > level::fatal ||
      !valid_text(message.category, message.message)) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    owned_event event;
    event.context = message.context;
    event.severity = message.severity;
    event.timestamp_ns = current_time_ns();
    event.thread_id = current_thread_id();
    event.source.assign(message.source);
    event.category.assign(message.category);
    event.message.assign(message.message);
    event.result = message.result;
    {
      const std::scoped_lock lock(mutex_);
      if (is_stopping_) {
        return gneiss::engine::core::result::invalid_state;
      }
      if (queue_.size() >= capacity_) {
        ++dropped_count_;
        return gneiss::engine::core::result::success;
      }
      event.sequence = next_sequence_++;
      queue_.push_back(std::move(event));
    }
    ready_.notify_one();
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

void log_dispatcher::flush() noexcept {
  std::unique_lock lock(mutex_);
  drained_.wait(lock, [this] { return queue_.empty() && !is_delivering_ && dropped_count_ == 0U; });
}

void log_dispatcher::run() noexcept {
  for (;;) {
    owned_event event;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return is_stopping_ || !queue_.empty() || dropped_count_ != 0U; });
      if (queue_.empty() && dropped_count_ == 0U && is_stopping_) {
        break;
      }
      if (!queue_.empty()) {
        event = std::move(queue_.front());
        queue_.pop_front();
      } else {
        const auto count = std::exchange(dropped_count_, 0U);
        event = make_drop_event(count);
      }
      is_delivering_ = true;
    }
    deliver(event);
    {
      const std::scoped_lock lock(mutex_);
      is_delivering_ = false;
      if (queue_.empty() && dropped_count_ == 0U) {
        drained_.notify_all();
      }
    }
  }
  drained_.notify_all();
}

void log_dispatcher::deliver(const owned_event& event) noexcept {
  const event_view borrowed{
      .message =
          {
              .context = event.context,
              .severity = event.severity,
              .source = event.source,
              .category = event.category,
              .message = event.message,
              .result = event.result,
          },
      .sequence = event.sequence,
      .timestamp_ns = event.timestamp_ns,
      .thread_id = event.thread_id,
  };
  try {
    is_in_log_callback = true;
    callback_(borrowed);
    is_in_log_callback = false;
  } catch (...) {
    is_in_log_callback = false;
  }
}

log_dispatcher::owned_event log_dispatcher::make_drop_event(std::uint64_t count) noexcept {
  owned_event event;
  try {
    event.severity = level::warning;
    event.sequence = next_sequence_++;
    event.timestamp_ns = current_time_ns();
    event.thread_id = current_thread_id();
    event.source = "engine.log";
    event.category = "backpressure";
    event.message = "日志队列已满，丢弃 " + std::to_string(count) + " 个事件";
    event.result = gneiss::engine::core::result::not_ready;
  } catch (...) {
    event.source.clear();
    event.category.clear();
    event.message.clear();
  }
  return event;
}

} // namespace gneiss::engine::core::log
