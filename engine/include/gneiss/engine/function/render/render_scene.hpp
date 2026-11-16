// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/math/types.hpp>
#include <gneiss/engine/core/types/rid.hpp>
namespace gneiss {
/** 持久渲染实例的值描述；不包含 World、资产对象或后端指针。 */
struct mesh_instance_description {
  rid mesh{};
  rid material{};
  math::matrix4 world_matrix{1.0F};
  std::uint64_t layers{~UINT64_C(0)};
  bool visible{true};
  bool casts_shadows{true};
};
}
