// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <cstdint>
namespace gneiss {
/** 单次主循环的时间快照；宿主模拟暂停不影响应用时间。 */
struct frame_time {
  std::uint64_t frame_index{};
  std::uint64_t delta_ns{};
  std::uint64_t elapsed_ns{};
};
} // namespace gneiss
