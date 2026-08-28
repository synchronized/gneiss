// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_math.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace {

[[nodiscard]] bool near(float left, float right) noexcept {
  return std::abs(left - right) <= 1.0e-4F;
}

[[nodiscard]] gneiss::transform combine(const gneiss::transform& parent,
                                        const gneiss::transform& local) noexcept {
  gneiss::transform result = GNEISS_TRANSFORM_IDENTITY;
  const std::array scaled{local.translation[0] * parent.scale[0],
                          local.translation[1] * parent.scale[1],
                          local.translation[2] * parent.scale[2]};
  const std::array uv = {(parent.rotation[1] * scaled[2]) - (parent.rotation[2] * scaled[1]),
                         (parent.rotation[2] * scaled[0]) - (parent.rotation[0] * scaled[2]),
                         (parent.rotation[0] * scaled[1]) - (parent.rotation[1] * scaled[0])};
  const std::array uuv = {(parent.rotation[1] * uv[2]) - (parent.rotation[2] * uv[1]),
                          (parent.rotation[2] * uv[0]) - (parent.rotation[0] * uv[2]),
                          (parent.rotation[0] * uv[1]) - (parent.rotation[1] * uv[0])};
  for (std::size_t index = 0; index < 3U; ++index) {
    result.translation[index] = parent.translation[index] + scaled[index] +
                                (2.0F * ((parent.rotation[3] * uv[index]) + uuv[index]));
    result.scale[index] = parent.scale[index] * local.scale[index];
  }
  result.rotation[0] =
      (parent.rotation[3] * local.rotation[0]) + (parent.rotation[0] * local.rotation[3]) +
      (parent.rotation[1] * local.rotation[2]) - (parent.rotation[2] * local.rotation[1]);
  result.rotation[1] =
      (parent.rotation[3] * local.rotation[1]) - (parent.rotation[0] * local.rotation[2]) +
      (parent.rotation[1] * local.rotation[3]) + (parent.rotation[2] * local.rotation[0]);
  result.rotation[2] =
      (parent.rotation[3] * local.rotation[2]) + (parent.rotation[0] * local.rotation[1]) -
      (parent.rotation[1] * local.rotation[0]) + (parent.rotation[2] * local.rotation[3]);
  result.rotation[3] =
      (parent.rotation[3] * local.rotation[3]) - (parent.rotation[0] * local.rotation[0]) -
      (parent.rotation[1] * local.rotation[1]) - (parent.rotation[2] * local.rotation[2]);
  return result;
}

} // namespace

int main() {
  gneiss::transform parent = GNEISS_TRANSFORM_IDENTITY;
  parent.translation[0] = 4.0F;
  parent.scale[0] = 2.0F;
  parent.scale[1] = 3.0F;
  parent.scale[2] = 4.0F;
  constexpr float half_sqrt = 0.70710678F;
  parent.rotation[2] = half_sqrt;
  parent.rotation[3] = half_sqrt;
  gneiss::transform local = GNEISS_TRANSFORM_IDENTITY;
  local.translation[0] = 1.0F;
  local.translation[1] = 2.0F;
  local.scale[0] = 1.5F;
  local.scale[1] = 0.5F;
  local.rotation[1] = half_sqrt;
  local.rotation[3] = half_sqrt;
  const auto world = combine(parent, local);
  gneiss::transform recovered = GNEISS_TRANSFORM_IDENTITY;
  if (gneiss::editor::world_to_local_transform(&parent, world, recovered) !=
      gneiss::result::success) {
    return 1;
  }
  for (std::size_t index = 0; index < 3U; ++index) {
    if (!near(recovered.translation[index], local.translation[index]) ||
        !near(recovered.scale[index], local.scale[index])) {
      return 2;
    }
  }
  for (std::size_t index = 0; index < 4U; ++index) {
    if (!near(recovered.rotation[index], local.rotation[index])) {
      return 3;
    }
  }
  auto negative = world;
  negative.scale[0] = -world.scale[0];
  if (gneiss::editor::world_to_local_transform(&parent, negative, recovered) !=
          gneiss::result::unsupported ||
      gneiss::editor::world_to_local_transform(nullptr, world, recovered) !=
          gneiss::result::success) {
    return 4;
  }
  gneiss::editor::gizmo_matrix matrix{};
  gneiss::transform matrix_roundtrip = GNEISS_TRANSFORM_IDENTITY;
  if (gneiss::editor::transform_to_gizmo_matrix(world, matrix) != gneiss::result::success ||
      gneiss::editor::gizmo_matrix_to_transform(matrix, matrix_roundtrip) !=
          gneiss::result::success) {
    return 5;
  }
  for (std::size_t index = 0; index < 3U; ++index) {
    if (!near(matrix_roundtrip.translation[index], world.translation[index]) ||
        !near(matrix_roundtrip.scale[index], world.scale[index])) {
      return 6;
    }
  }
  for (std::size_t index = 0; index < 4U; ++index) {
    if (!near(std::abs(matrix_roundtrip.rotation[index]), std::abs(world.rotation[index]))) {
      return 7;
    }
  }
  matrix[0] = -matrix[0];
  matrix[1] = -matrix[1];
  matrix[2] = -matrix[2];
  if (gneiss::editor::gizmo_matrix_to_transform(matrix, matrix_roundtrip) !=
      gneiss::result::unsupported) {
    return 8;
  }
  return 0;
}
