// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "game_update_scheduler.h"

#include <array>
#include <cstdint>

namespace {

struct trace final {
  std::array<gneiss_game_update_time, 16> fixed{};
  std::array<gneiss_game_update_time, 8> variable{};
  std::uint32_t fixed_count{};
  std::uint32_t variable_count{};
  gneiss::result fixed_result = gneiss::result::success;
  gneiss::result variable_result = gneiss::result::success;
};

gneiss::result fixed_update(void* user_data, const gneiss_game_update_time& time) noexcept {
  auto& value = *static_cast<trace*>(user_data);
  value.fixed[value.fixed_count++] = time;
  return value.fixed_result;
}

gneiss::result update(void* user_data, const gneiss_game_update_time& time) noexcept {
  auto& value = *static_cast<trace*>(user_data);
  value.variable[value.variable_count++] = time;
  return value.variable_result;
}

} // namespace

int main() {
  using namespace gneiss::runtime_internal;
  game_update_scheduler scheduler({10U, 50U, 3U});
  trace calls;
  const game_update_callbacks callbacks{&calls, fixed_update, update};
  game_update_report report;

  gneiss_frame_time frame{0U, 5U, 5U, 0U, {}};
  if (scheduler.advance(frame, callbacks, report) != gneiss::result::success ||
      report.fixed_step_count != 0U || calls.variable_count != 1U ||
      calls.variable[0].delta_ns != 5U) {
    return 1;
  }
  frame = {1U, 25U, 30U, 0U, {}};
  if (scheduler.advance(frame, callbacks, report) != gneiss::result::success ||
      report.fixed_step_count != 3U || report.dropped_ns != 0U || calls.fixed_count != 3U ||
      calls.fixed[0].update_index != 0U || calls.fixed[2].elapsed_ns != 30U ||
      calls.variable_count != 2U || calls.variable[1].update_index != 1U) {
    return 2;
  }
  frame = {2U, 100U, 130U, 0U, {}};
  if (scheduler.advance(frame, callbacks, report) != gneiss::result::success ||
      !report.was_frame_clamped || report.accepted_delta_ns != 50U ||
      report.fixed_step_count != 3U || report.dropped_ns != 20U || calls.fixed_count != 6U ||
      calls.variable_count != 3U || calls.variable[2].elapsed_ns != 80U) {
    return 3;
  }
  frame = {3U, 0U, 130U, 1U, {}};
  if (scheduler.advance(frame, callbacks, report) != gneiss::result::success ||
      report.fixed_step_count != 0U || calls.variable_count != 4U ||
      calls.variable[3].delta_ns != 0U) {
    return 4;
  }

  calls.fixed_result = gneiss::result::internal;
  frame = {4U, 10U, 140U, 0U, {}};
  if (scheduler.advance(frame, callbacks, report) != gneiss::result::internal ||
      calls.variable_count != 4U) {
    return 5;
  }
  scheduler.reset();
  calls.fixed_result = gneiss::result::success;
  calls.variable_result = gneiss::result::dependency_failed;
  frame = {0U, 0U, 0U, 0U, {}};
  if (scheduler.advance(frame, callbacks, report) != gneiss::result::dependency_failed ||
      calls.variable_count != 5U || calls.variable[4].update_index != 0U) {
    return 6;
  }

  game_update_scheduler invalid({0U, 1U, 1U});
  if (invalid.advance(frame, callbacks, report) != gneiss::result::invalid_argument) {
    return 7;
  }
  return 0;
}
