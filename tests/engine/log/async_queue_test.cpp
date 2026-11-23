// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>
#include "core/logging/detail/async_queue.hpp"
#include <new>
#include <string_view>

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

void capture(const gneiss::logging::record_view& event, void* user_data) {
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
  using namespace gneiss;
  unsigned rejected{};
  try {
    logging::detail::async_queue invalid(logging::record_callback{});
    return false;
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  try {
    logging::detail::async_queue invalid([](const logging::record_view&) {}, 0U);
    return false;
  } catch (const std::invalid_argument&) {
    ++rejected;
  }
  std::uint32_t calls{};
  bool valid = true;
  logging::detail::async_queue* active{};
  {
    logging::detail::async_queue dispatcher([&](const logging::record_view& event) {
      ++calls;
      valid = valid && event.message.context == 9U && event.thread_id != 0U &&
              event.timestamp_ns != 0U && event.sequence == calls &&
              active->submit({.category = "nested"}) == gneiss::error::invalid_state;
    });
    active = &dispatcher;
    if (dispatcher.submit({.context = 9U, .category = "test"}) != gneiss::error::success ||
        dispatcher.submit({.context = 9U, .category = "test"}) != gneiss::error::success) {
      return false;
    }
    // 不主动 flush，验证析构排空队列。
  }
  return rejected == 2U && calls == 2U && valid;
}

bool verify_concurrency() {
  using namespace gneiss;
  std::uint64_t received{};
  bool valid = true;
  logging::detail::async_queue dispatcher(
      [&](const logging::record_view& event) {
        ++received;
        valid = valid && event.sequence == received &&
                event.message.severity == logging::level::debug &&
                event.message.result == gneiss::error::not_found &&
                event.message.message == "parallel";
      },
      512U);
  std::atomic<bool> accepted{true};
  {
    std::array<std::jthread, 4> producers;
    for (auto& producer : producers) {
      producer = std::jthread([&] {
        for (unsigned index = 0; index < 64U; ++index) {
          if (dispatcher.submit({
                  .severity = logging::level::debug,
                  .category = "test",
                  .message = "parallel",
                  .result = gneiss::error::not_found,
              }) != gneiss::error::success) {
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

int main(int argc, char** argv) try {
  gneiss::install_fatal_handler();
  if (argc > 1) {
    const bool oom = std::string_view(argv[1]) == "oom";
    gneiss::logging::detail::async_queue dispatcher([oom](const auto&) {
      if (oom)
        throw std::bad_alloc{};
      throw std::runtime_error("unexpected callback");
    });
    if (dispatcher.submit({.category = "fatal-test"}).failed())
      return 10;
    dispatcher.flush();
    return 0;
  }
  if (!verify_failure_and_shutdown() || !verify_concurrency()) {
    return 5;
  }
  capture_state state;
  gneiss::logging::detail::async_queue dispatcher(
      [&state](const auto& event) { capture(event, &state); }, 2U);
  std::string payload = "event";
  const gneiss::logging::message_view message{
      .context = 1U,
      .source = "test",
      .category = "test",
      .message = payload,
  };
  if (dispatcher.submit(message) != gneiss::error::success) {
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
    if (dispatcher.submit(message) != gneiss::error::success) {
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
