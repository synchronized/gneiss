// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <array>

namespace gneiss::engine::core::math {
/** 纯值 TRS；四元数顺序为 xyzw，不持有场景或后端状态。 */
struct transform {
  std::array<float, 3> translation{};
  std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
  std::array<float, 3> scale{1.0F, 1.0F, 1.0F};
};
} // namespace gneiss::engine::core::math
