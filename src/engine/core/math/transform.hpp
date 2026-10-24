// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/types.hpp"

namespace gneiss::engine {
/** 纯值 TRS；四元数分量访问及序列化顺序为 xyzw，构造顺序为 wxyz，不持有场景或后端状态。 */
struct transform {
  vector3 translation{0.0F};
  quaternion rotation{1.0F, 0.0F, 0.0F, 0.0F};
  vector3 scale{1.0F};
};
} // namespace gneiss::engine
