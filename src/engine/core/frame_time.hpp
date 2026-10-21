// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>
namespace gneiss::engine::core {
/** 单次主循环的时间快照；暂停时 delta 为零。 */
struct frame_time {
  std::uint64_t frame_index{};
  std::uint64_t delta_ns{};
  std::uint64_t elapsed_ns{};
  bool is_paused{};
};
} // namespace gneiss::engine::core
