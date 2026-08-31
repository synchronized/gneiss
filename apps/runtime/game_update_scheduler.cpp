// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "game_update_scheduler.h"

#include <algorithm>
#include <limits>

namespace {

[[nodiscard]] std::uint64_t saturated_add(std::uint64_t left, std::uint64_t right) noexcept {
  return right > std::numeric_limits<std::uint64_t>::max() - left
             ? std::numeric_limits<std::uint64_t>::max()
             : left + right;
}

} // namespace

namespace gneiss::runtime_internal {

game_update_scheduler::game_update_scheduler(game_update_scheduler_config config) noexcept
    : config_(config) {}

result game_update_scheduler::advance(const gneiss_frame_time& frame,
                                      const game_update_callbacks& callbacks,
                                      game_update_report& out_report) noexcept {
  out_report = {};
  if (config_.fixed_delta_ns == 0U || config_.maximum_frame_delta_ns == 0U ||
      config_.maximum_fixed_steps == 0U || callbacks.fixed_update == nullptr ||
      callbacks.update == nullptr) {
    return result::invalid_argument;
  }

  const auto accepted_delta = std::min(frame.delta_ns, config_.maximum_frame_delta_ns);
  out_report.accepted_delta_ns = accepted_delta;
  out_report.was_frame_clamped = accepted_delta != frame.delta_ns;
  accumulator_ns_ = saturated_add(accumulator_ns_, accepted_delta);
  update_elapsed_ns_ = saturated_add(update_elapsed_ns_, accepted_delta);

  while (accumulator_ns_ >= config_.fixed_delta_ns &&
         out_report.fixed_step_count < config_.maximum_fixed_steps) {
    fixed_elapsed_ns_ = saturated_add(fixed_elapsed_ns_, config_.fixed_delta_ns);
    const gneiss_game_update_time time = {sizeof(gneiss_game_update_time), 0U, fixed_update_index_,
                                          config_.fixed_delta_ns, fixed_elapsed_ns_};
    const auto update_result = callbacks.fixed_update(callbacks.user_data, time);
    if (update_result != result::success) {
      return update_result;
    }
    accumulator_ns_ -= config_.fixed_delta_ns;
    ++fixed_update_index_;
    ++out_report.fixed_step_count;
  }

  if (accumulator_ns_ >= config_.fixed_delta_ns) {
    out_report.dropped_ns = accumulator_ns_ - accumulator_ns_ % config_.fixed_delta_ns;
    accumulator_ns_ %= config_.fixed_delta_ns;
  }

  const gneiss_game_update_time time = {sizeof(gneiss_game_update_time), 0U, update_index_,
                                        accepted_delta, update_elapsed_ns_};
  const auto update_result = callbacks.update(callbacks.user_data, time);
  if (update_result == result::success) {
    ++update_index_;
  }
  return update_result;
}

void game_update_scheduler::reset() noexcept {
  accumulator_ns_ = 0U;
  fixed_elapsed_ns_ = 0U;
  update_elapsed_ns_ = 0U;
  fixed_update_index_ = 0U;
  update_index_ = 0U;
}

} // namespace gneiss::runtime_internal
