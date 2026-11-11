// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "async_queue.hpp"
#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"

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

namespace gneiss::logging::detail {

async_queue::async_queue(record_callback callback, std::size_t capacity)
    : callback_(std::move(callback)), capacity_(capacity) {
  if (!callback_ || capacity_ == 0U) {
    throw std::invalid_argument("日志投递器配置无效");
  }
  worker_ = std::thread(&async_queue::run, this);
}

async_queue::~async_queue() noexcept {
  {
    const std::scoped_lock lock(mutex_);
    is_stopping_ = true;
  }
  ready_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

gneiss::error async_queue::submit(const message_view& message) {
  if (is_in_log_callback) {
    return gneiss::error::invalid_state;
  }
  {
    owned_record event;
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
        return gneiss::error::invalid_state;
      }
      if (queue_.size() >= capacity_) {
        ++dropped_count_;
        return gneiss::error::success;
      }
      event.sequence = next_sequence_++;
      queue_.push_back(std::move(event));
    }
    ready_.notify_one();
    return gneiss::error::success;
  }
}

void async_queue::flush() noexcept {
  std::unique_lock lock(mutex_);
  drained_.wait(lock, [this] { return queue_.empty() && !is_delivering_ && dropped_count_ == 0U; });
}

void async_queue::run() noexcept try {
  for (;;) {
    owned_record event;
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
} catch (...) {
  gneiss::fatal_current_exception();
}

void async_queue::deliver(const owned_record& event) {
  const record_view borrowed{
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
    throw;
  }
}

async_queue::owned_record async_queue::make_drop_event(std::uint64_t count) {
  owned_record event;
  {
    event.severity = level::warning;
    event.sequence = next_sequence_++;
    event.timestamp_ns = current_time_ns();
    event.thread_id = current_thread_id();
    event.source = "engine.log";
    event.category = "backpressure";
    event.message = "日志队列已满，丢弃 " + std::to_string(count) + " 个事件";
    event.result = gneiss::error::not_ready;
  }
  return event;
}

} // namespace gneiss::logging
