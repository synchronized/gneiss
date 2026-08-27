// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/normal_math.h"

#include <cmath>

int main() {
  constexpr auto half_sqrt_two = 0.7071067811865475F;
  const gneiss_mesh_normal normal{.x = half_sqrt_two, .y = half_sqrt_two, .z = 0.0F};
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.scale[0] = 2.0F;
  transform.rotation[2] = half_sqrt_two;
  transform.rotation[3] = half_sqrt_two;
  const auto result = gneiss::render_internal::transform_normal(normal, transform);
  const auto close = [](float left, float right) { return std::abs(left - right) < 1.0e-5F; };
  return close(result[0], -0.8944271909999159F) && close(result[1], 0.4472135954999579F) &&
                 close(result[2], 0.0F)
             ? 0
             : 1;
}
