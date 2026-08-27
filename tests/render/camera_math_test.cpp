// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/camera_math.h"

#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr float tolerance = 0.0001F;

bool nearly_equal(float left, float right) noexcept { return std::abs(left - right) <= tolerance; }

} // namespace

int main() {
  using gneiss::render_internal::build_view_matrix;
  using gneiss::render_internal::build_vulkan_perspective_matrix;
  using gneiss::render_internal::matrix4;
  using gneiss::render_internal::transform_vector;

  matrix4 view;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[2] = 2.0F;
  if (build_view_matrix(transform, view) != GNEISS_SUCCESS) {
    return 1;
  }
  const auto translated_origin = transform_vector(view, {0.0F, 0.0F, 0.0F, 1.0F});
  if (!nearly_equal(translated_origin[2], -2.0F) || !nearly_equal(translated_origin[3], 1.0F)) {
    return 2;
  }

  constexpr float half_sqrt_two = 0.70710678F;
  transform = GNEISS_TRANSFORM_IDENTITY;
  transform.rotation[1] = half_sqrt_two;
  transform.rotation[3] = half_sqrt_two;
  transform.scale[0] = 8.0F;
  transform.scale[1] = 9.0F;
  transform.scale[2] = 10.0F;
  if (build_view_matrix(transform, view) != GNEISS_SUCCESS) {
    return 3;
  }
  const auto camera_forward_point = transform_vector(view, {-2.0F, 0.0F, 0.0F, 1.0F});
  if (!nearly_equal(camera_forward_point[0], 0.0F) ||
      !nearly_equal(camera_forward_point[2], -2.0F)) {
    return 4;
  }

  gneiss_camera camera = GNEISS_CAMERA_INIT;
  camera.vertical_field_of_view_radians = 1.57079633F;
  camera.near_plane = 0.1F;
  camera.far_plane = 100.0F;
  matrix4 projection;
  if (build_vulkan_perspective_matrix(camera, 2.0F, projection) != GNEISS_SUCCESS) {
    return 5;
  }
  const auto near_point = transform_vector(projection, {0.0F, 0.0F, -camera.near_plane, 1.0F});
  const auto far_point = transform_vector(projection, {0.0F, 0.0F, -camera.far_plane, 1.0F});
  const auto upper_point = transform_vector(projection, {0.0F, 1.0F, -1.0F, 1.0F});
  if (!nearly_equal(near_point[2] / near_point[3], 0.0F) ||
      !nearly_equal(far_point[2] / far_point[3], 1.0F) || upper_point[1] >= 0.0F) {
    return 6;
  }

  transform.rotation[0] = 0.0F;
  transform.rotation[1] = 0.0F;
  transform.rotation[2] = 0.0F;
  transform.rotation[3] = 0.0F;
  if (build_view_matrix(transform, view) != GNEISS_ERROR_INVALID_ARGUMENT ||
      build_vulkan_perspective_matrix(camera, 0.0F, projection) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 7;
  }
  camera.far_plane = std::numeric_limits<float>::infinity();
  return build_vulkan_perspective_matrix(camera, 1.0F, projection) == GNEISS_ERROR_INVALID_ARGUMENT
             ? 0
             : 8;
}
