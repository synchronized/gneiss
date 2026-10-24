// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/normal_math.hpp"

#include <cmath>

namespace gneiss::engine {

vector3 transform_normal(const vector3& normal,
                         const gneiss::engine::transform& transform) noexcept {
  const auto value =
      glm::normalize(transform.rotation *
                     (gneiss::engine::vector3{normal[0], normal[1], normal[2]} / transform.scale));
  return {value.x, value.y, value.z};
}

} // namespace gneiss::engine
