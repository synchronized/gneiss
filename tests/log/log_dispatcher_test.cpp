// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/log/log_dispatcher.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace {

struct capture_state final {
  std::mutex mutex;
  std::condition_variable changed;
  std::uint64_t event_count = 0U;
  std::uint64_t previous_sequence = 0U;
  std::uint64_t dropped_reports = 0U;
  bool block_first = true;
  bool first_started = false;
};

void capture(const gneiss::log_internal::event_view& event, void* user_data) {
  auto& state = *static_cast<capture_state*>(user_data);
  std::unique_lock lock(state.mutex);
  if (state.block_first) {
    state.first_started = true;
    state.changed.notify_all();
    state.changed.wait(lock, [&state] { return !state.block_first; });
  }
  if (event.sequence <= state.previous_sequence) {
    state.previous_sequence = UINT64_MAX;
  } else {
    state.previous_sequence = event.sequence;
  }
  ++state.event_count;
  if (event.message.category == "backpressure") {
    ++state.dropped_reports;
  }
  state.changed.notify_all();
}

bool verify_failure_and_shutdown() {
  using namespace gneiss::log_internal;
  try {
    log_dispatcher invalid(event_sink{});
    return false;
  } catch (const std::invalid_argument&) {
  }
  try {
    log_dispatcher invalid([](const event_view&) {}, 0U);
    return false;
  } catch (const std::invalid_argument&) {
  }
  std::uint32_t calls{};
  bool valid = true;
  log_dispatcher* active{};
  {
    log_dispatcher dispatcher([&](const event_view& event) {
      ++calls;
      valid = valid && event.message.context == 9U && event.thread_id != 0U &&
              event.timestamp_ns != 0U && event.sequence == calls &&
              active->submit({.category = "nested"}) == GNEISS_ERROR_INVALID_STATE;
      if (calls == 1U) {
        throw std::runtime_error("测试回调异常隔离");
      }
    });
    active = &dispatcher;
    if (dispatcher.submit({.context = 9U, .category = "test"}) != GNEISS_SUCCESS ||
        dispatcher.submit({.context = 9U, .category = "test"}) != GNEISS_SUCCESS) {
      return false;
    }
    // 不主动 flush，验证析构排空队列且异常不会阻止后续投递。
  }
  return calls == 2U && valid;
}

} // namespace

int main() try {
  if (!verify_failure_and_shutdown()) {
    return 5;
  }
  capture_state state;
  gneiss::log_internal::log_dispatcher dispatcher(
      [&state](const auto& event) { capture(event, &state); }, 2U);
  const gneiss::log_internal::message_view message{
      .context = 1U, .source = "test", .category = "test", .message = "event"};
  if (dispatcher.submit(message) != GNEISS_SUCCESS) {
    return 1;
  }
  {
    std::unique_lock lock(state.mutex);
    if (!state.changed.wait_for(lock, std::chrono::seconds(2),
                                [&state] { return state.first_started; })) {
      return 2;
    }
  }
  for (std::uint32_t index = 0U; index < 8U; ++index) {
    if (dispatcher.submit(message) != GNEISS_SUCCESS) {
      return 3;
    }
  }
  {
    const std::scoped_lock lock(state.mutex);
    state.block_first = false;
  }
  state.changed.notify_all();
  dispatcher.flush();
  {
    const std::scoped_lock lock(state.mutex);
    if (state.event_count != 4U || state.dropped_reports != 1U || state.previous_sequence != 4U) {
      return 4;
    }
  }
  return 0;
} catch (...) {
  return 6;
}
