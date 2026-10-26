// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "viewport/transform_gizmo_math.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace {

[[nodiscard]] bool near(float left, float right) noexcept {
  return std::abs(left - right) <= 1.0e-4F;
}

[[nodiscard]] gneiss::engine::transform combine(const gneiss::engine::transform& parent,
                                                const gneiss::engine::transform& local) noexcept {
  gneiss::engine::transform result{};
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
  gneiss::engine::transform parent{};
  parent.translation[0] = 4.0F;
  parent.scale[0] = 2.0F;
  parent.scale[1] = 3.0F;
  parent.scale[2] = 4.0F;
  constexpr float half_sqrt = 0.70710678F;
  parent.rotation[2] = half_sqrt;
  parent.rotation[3] = half_sqrt;
  gneiss::engine::transform local{};
  local.translation[0] = 1.0F;
  local.translation[1] = 2.0F;
  local.scale[0] = 1.5F;
  local.scale[1] = 0.5F;
  local.rotation[1] = half_sqrt;
  local.rotation[3] = half_sqrt;
  const auto world = combine(parent, local);
  gneiss::engine::transform recovered{};
  if (gneiss::editor::world_to_local_transform(&parent, world, recovered) !=
      gneiss::engine::result::success) {
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
          gneiss::engine::result::unsupported ||
      gneiss::editor::world_to_local_transform(nullptr, world, recovered) !=
          gneiss::engine::result::success) {
    return 4;
  }
  gneiss::editor::gizmo_matrix matrix{};
  gneiss::engine::transform matrix_roundtrip{};
  if (gneiss::editor::transform_to_gizmo_matrix(world, matrix) != gneiss::engine::result::success ||
      gneiss::editor::gizmo_matrix_to_transform(matrix, matrix_roundtrip) !=
          gneiss::engine::result::success) {
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
      gneiss::engine::result::unsupported) {
    return 8;
  }
  // Gizmo 只接收仿射 TRS；透视行不能被静默忽略，失败不得修改输出。
  if (gneiss::editor::transform_to_gizmo_matrix(world, matrix) != gneiss::engine::result::success) {
    return 9;
  }
  matrix[3] = 0.25F;
  matrix_roundtrip.translation[0] = 123.0F;
  if (gneiss::editor::gizmo_matrix_to_transform(matrix, matrix_roundtrip) !=
          gneiss::engine::result::invalid_argument ||
      matrix_roundtrip.translation[0] != 123.0F) {
    return 10;
  }
  // 相机适配保持右手、Y 向上投影与忽略缩放的视图语义。
  gneiss::engine::transform camera;
  camera.translation.z = 2.0F;
  camera.scale = {2, 3, 4};
  const auto view = gneiss::editor::build_gizmo_view_matrix(camera);
  const auto projection = gneiss::editor::build_gizmo_projection_matrix(2.0F);
  if (!near(view[0], 1.0F) || !near(view[14], -2.0F) ||
      !near(projection[0], std::sqrt(3.0F) / 2.0F) || !near(projection[5], std::sqrt(3.0F)) ||
      !near(projection[11], -1.0F))
    return 11;
  camera.rotation = {0, 0, 0, 0};
  if (gneiss::editor::build_gizmo_view_matrix(camera) != gneiss::editor::gizmo_matrix{} ||
      gneiss::editor::build_gizmo_projection_matrix(0) != gneiss::editor::gizmo_matrix{})
    return 12;
  return 0;
}
