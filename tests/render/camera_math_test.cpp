// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/camera.hpp"
#include "engine/core/result.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr float tolerance = 0.0001F;

bool nearly_equal(float left, float right) noexcept { return std::abs(left - right) <= tolerance; }

} // namespace

int main() {
  using gneiss::engine::math::matrix4;
  using gneiss::engine::math::perspective_rh_zo;
  using gneiss::engine::math::vector4;
  using gneiss::engine::math::view_matrix_rh;

  matrix4 view;
  gneiss::engine::math::transform transform = {};
  transform.translation[2] = 2.0F;
  if (view_matrix_rh(transform, view) != gneiss::engine::result::success) {
    return 1;
  }
  const auto translated_origin = view * vector4{0.0F, 0.0F, 0.0F, 1.0F};
  if (!nearly_equal(translated_origin[2], -2.0F) || !nearly_equal(translated_origin[3], 1.0F)) {
    return 2;
  }

  constexpr float half_sqrt_two = 0.70710678F;
  transform = {};
  transform.rotation[1] = half_sqrt_two;
  transform.rotation[3] = half_sqrt_two;
  transform.scale[0] = 8.0F;
  transform.scale[1] = 9.0F;
  transform.scale[2] = 10.0F;
  if (view_matrix_rh(transform, view) != gneiss::engine::result::success) {
    return 3;
  }
  const auto camera_forward_point = view * vector4{-2.0F, 0.0F, 0.0F, 1.0F};
  if (!nearly_equal(camera_forward_point[0], 0.0F) ||
      !nearly_equal(camera_forward_point[2], -2.0F)) {
    return 4;
  }

  gneiss::engine::math::camera_settings camera;
  camera.fov = 1.57079633F;
  camera.near = 0.1F;
  camera.far = 100.0F;
  matrix4 projection;
  if (perspective_rh_zo(camera, 2.0F, projection) != gneiss::engine::result::success) {
    return 5;
  }
  const auto near_point = projection * vector4{0.0F, 0.0F, -camera.near, 1.0F};
  const auto far_point = projection * vector4{0.0F, 0.0F, -camera.far, 1.0F};
  const auto upper_point = projection * vector4{0.0F, 1.0F, -1.0F, 1.0F};
  const auto lower_point = projection * vector4{0.0F, -1.0F, -1.0F, 1.0F};
  if (!nearly_equal(near_point[2] / near_point[3], 0.0F) ||
      !nearly_equal(far_point[2] / far_point[3], 1.0F) ||
      !nearly_equal(upper_point[1] / upper_point[3], 1.0F) ||
      !nearly_equal(lower_point[1] / lower_point[3], -1.0F)) {
    return 6;
  }

  const auto previous_view = view;
  const auto previous_projection = projection;
  transform.rotation[0] = 0.0F;
  transform.rotation[1] = 0.0F;
  transform.rotation[2] = 0.0F;
  transform.rotation[3] = 0.0F;
  if (view_matrix_rh(transform, view) != gneiss::engine::result::invalid_argument ||
      perspective_rh_zo(camera, 0.0F, projection) != gneiss::engine::result::invalid_argument ||
      view != previous_view || projection != previous_projection) {
    return 7;
  }
  camera.far = std::numeric_limits<float>::infinity();
  return perspective_rh_zo(camera, 1.0F, projection) == gneiss::engine::result::invalid_argument
             ? 0
             : 8;
}
