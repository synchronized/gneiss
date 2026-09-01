// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "log/log_dispatcher.h"

#include <gneiss/log.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
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

void capture(gneiss_application, const gneiss_log_event* event, void* user_data) {
  auto& state = *static_cast<capture_state*>(user_data);
  std::unique_lock lock(state.mutex);
  if (state.block_first) {
    state.first_started = true;
    state.changed.notify_all();
    state.changed.wait(lock, [&state] { return !state.block_first; });
  }
  if (event->sequence <= state.previous_sequence) {
    state.previous_sequence = UINT64_MAX;
  } else {
    state.previous_sequence = event->sequence;
  }
  ++state.event_count;
  if (std::string_view(event->category, event->category_length) == "backpressure") {
    ++state.dropped_reports;
  }
  state.changed.notify_all();
}

} // namespace

int main() {
  capture_state state;
  gneiss::log_internal::log_dispatcher dispatcher(capture, &state, 2U);
  const auto message = gneiss::make_log_message(gneiss::log_severity::info, "test", "event");
  if (dispatcher.submit(1U, message, "test") != GNEISS_SUCCESS) {
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
    if (dispatcher.submit(1U, message, "test") != GNEISS_SUCCESS) {
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
}
