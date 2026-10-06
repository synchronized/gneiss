// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_RUNTIME_GAME_UPDATE_SCHEDULER_H_
#define GNEISS_APPS_RUNTIME_GAME_UPDATE_SCHEDULER_H_

#include <gneiss/application.h>
#include <gneiss/core/result.hpp>
#include <gneiss/game_module.h>

#include <cstdint>

namespace gneiss::runtime_internal {

struct game_update_scheduler_config final {
  std::uint64_t fixed_delta_ns = UINT64_C(16666667);
  std::uint64_t maximum_frame_delta_ns = UINT64_C(250000000);
  std::uint32_t maximum_fixed_steps = UINT32_C(8);
};

struct game_update_callbacks final {
  void* user_data{};
  result (*fixed_update)(void* user_data, const gneiss_game_update_time& time) noexcept {};
  result (*update)(void* user_data, const gneiss_game_update_time& time) noexcept {};
};

struct game_update_report final {
  std::uint32_t fixed_step_count{};
  std::uint64_t dropped_ns{};
  std::uint64_t accepted_delta_ns{};
  bool was_frame_clamped{};
};

// 将 Application 帧时间转换为有界固定更新和单次逐帧更新；调用方负责外部同步。
class game_update_scheduler final {
public:
  explicit game_update_scheduler(game_update_scheduler_config config = {}) noexcept;

  [[nodiscard]] result advance(const gneiss_frame_time& frame,
                               const game_update_callbacks& callbacks,
                               game_update_report& out_report) noexcept;
  void reset() noexcept;

private:
  game_update_scheduler_config config_;
  std::uint64_t accumulator_ns_{};
  std::uint64_t fixed_elapsed_ns_{};
  std::uint64_t update_elapsed_ns_{};
  std::uint64_t fixed_update_index_{};
  std::uint64_t update_index_{};
};

} // namespace gneiss::runtime_internal

#endif
