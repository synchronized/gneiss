// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/log/log_dispatcher.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct capture_state final {
  std::mutex mutex;
  std::condition_variable changed;
  std::uint64_t event_count = 0U;
  std::uint64_t previous_sequence = 0U;
  std::uint64_t dropped_reports = 0U;
  bool block_first = true;
  bool first_started = false;
  bool valid_message = true;
};

void capture(const gneiss::engine::event_view& event, void* user_data) {
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
  } else {
    state.valid_message = state.valid_message && event.message.message == "event";
  }
  state.changed.notify_all();
}

bool verify_failure_and_shutdown() {
  using namespace gneiss::engine;
  unsigned rejected{};
  try {
    log_dispatcher invalid(event_sink{});
    return false;
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  try {
    log_dispatcher invalid([](const event_view&) {}, 0U);
    return false;
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  std::uint32_t calls{};
  bool valid = true;
  log_dispatcher* active{};
  {
    log_dispatcher dispatcher([&](const event_view& event) {
      ++calls;
      valid = valid && event.message.context == 9U && event.thread_id != 0U &&
              event.timestamp_ns != 0U && event.sequence == calls &&
              active->submit({.category = "nested"}) == gneiss::engine::error::invalid_state;
      if (calls == 1U) {
        throw std::runtime_error("测试回调异常隔离");
      }
    });
    active = &dispatcher;
    if (dispatcher.submit({.context = 9U, .category = "test"}) != gneiss::engine::error::success ||
        dispatcher.submit({.context = 9U, .category = "test"}) != gneiss::engine::error::success) {
      return false;
    }
    // 不主动 flush，验证析构排空队列且异常不会阻止后续投递。
  }
  return rejected == 2U && calls == 2U && valid;
}

bool verify_validation_and_concurrency() {
  using namespace gneiss::engine;
  std::uint64_t received{};
  bool valid = true;
  log_dispatcher dispatcher(
      [&](const event_view& event) {
        ++received;
        valid = valid && event.sequence == received && event.message.severity == level::debug &&
                event.message.result == gneiss::engine::error::not_found &&
                event.message.message == "parallel";
      },
      512U);
  if (dispatcher.submit({.severity = static_cast<level>(0), .category = "test"}) !=
          gneiss::engine::error::invalid_argument ||
      dispatcher.submit({.severity = static_cast<level>(7), .category = "test"}) !=
          gneiss::engine::error::invalid_argument ||
      dispatcher.submit({.category = ""}) != gneiss::engine::error::invalid_argument ||
      dispatcher.submit({.category = "test", .message = "\xff"}) !=
          gneiss::engine::error::invalid_argument) {
    return false;
  }
  std::atomic<bool> accepted{true};
  {
    std::array<std::jthread, 4> producers;
    for (auto& producer : producers) {
      producer = std::jthread([&] {
        for (unsigned index = 0; index < 64U; ++index) {
          if (dispatcher.submit({
                  .severity = level::debug,
                  .category = "test",
                  .message = "parallel",
                  .result = gneiss::engine::error::not_found,
              }) != gneiss::engine::error::success) {
            accepted.store(false);
          }
        }
      });
    }
  }
  dispatcher.flush();
  return accepted.load() && valid && received == 256U;
}

} // namespace

int main() try {
  if (!verify_failure_and_shutdown() || !verify_validation_and_concurrency()) {
    return 5;
  }
  capture_state state;
  gneiss::engine::log_dispatcher dispatcher([&state](const auto& event) { capture(event, &state); },
                                            2U);
  std::string payload = "event";
  const gneiss::engine::message_view message{
      .context = 1U,
      .source = "test",
      .category = "test",
      .message = payload,
  };
  if (dispatcher.submit(message) != gneiss::engine::error::success) {
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
    if (dispatcher.submit(message) != gneiss::engine::error::success) {
      return 3;
    }
  }
  {
    const std::scoped_lock lock(state.mutex);
    payload.assign("changed after submit");
    state.block_first = false;
  }
  state.changed.notify_all();
  dispatcher.flush();
  {
    const std::scoped_lock lock(state.mutex);
    if (!state.valid_message || state.event_count != 4U || state.dropped_reports != 1U ||
        state.previous_sequence != 4U) {
      return 4;
    }
  }
  return 0;
} catch (...) {
  return 6;
}
