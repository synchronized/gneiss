// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/camera_math.h"

#include <cmath>
#include <numbers>

namespace gneiss::render_internal {
namespace {

constexpr std::size_t matrix_index(std::size_t row, std::size_t column) noexcept {
  return (column * 4U) + row;
}

} // namespace

gneiss_result build_view_matrix(const gneiss_transform& transform, matrix4& out_matrix) noexcept {
  const auto x = transform.rotation[0];
  const auto y = transform.rotation[1];
  const auto z = transform.rotation[2];
  const auto w = transform.rotation[3];
  const auto length_squared = (x * x) + (y * y) + (z * z) + (w * w);
  if (!std::isfinite(length_squared) || length_squared <= 0.0F ||
      !std::isfinite(transform.translation[0]) || !std::isfinite(transform.translation[1]) ||
      !std::isfinite(transform.translation[2])) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  const auto inverse_length = 1.0F / std::sqrt(length_squared);
  const auto qx = -x * inverse_length;
  const auto qy = -y * inverse_length;
  const auto qz = -z * inverse_length;
  const auto qw = w * inverse_length;
  const auto xx = qx * qx;
  const auto yy = qy * qy;
  const auto zz = qz * qz;
  const auto xy = qx * qy;
  const auto xz = qx * qz;
  const auto yz = qy * qz;
  const auto wx = qw * qx;
  const auto wy = qw * qy;
  const auto wz = qw * qz;

  matrix4 result;
  result.values[matrix_index(0U, 0U)] = 1.0F - (2.0F * (yy + zz));
  result.values[matrix_index(0U, 1U)] = 2.0F * (xy - wz);
  result.values[matrix_index(0U, 2U)] = 2.0F * (xz + wy);
  result.values[matrix_index(1U, 0U)] = 2.0F * (xy + wz);
  result.values[matrix_index(1U, 1U)] = 1.0F - (2.0F * (xx + zz));
  result.values[matrix_index(1U, 2U)] = 2.0F * (yz - wx);
  result.values[matrix_index(2U, 0U)] = 2.0F * (xz - wy);
  result.values[matrix_index(2U, 1U)] = 2.0F * (yz + wx);
  result.values[matrix_index(2U, 2U)] = 1.0F - (2.0F * (xx + yy));
  result.values[matrix_index(3U, 3U)] = 1.0F;

  for (std::size_t row = 0; row < 3U; ++row) {
    result.values[matrix_index(row, 3U)] =
        -((result.values[matrix_index(row, 0U)] * transform.translation[0]) +
          (result.values[matrix_index(row, 1U)] * transform.translation[1]) +
          (result.values[matrix_index(row, 2U)] * transform.translation[2]));
  }
  out_matrix = result;
  return GNEISS_SUCCESS;
}

gneiss_result build_vulkan_perspective_matrix(const gneiss_camera& camera, float aspect,
                                              matrix4& out_matrix) noexcept {
  if (!std::isfinite(aspect) || aspect <= 0.0F ||
      !std::isfinite(camera.vertical_field_of_view_radians) || !std::isfinite(camera.near_plane) ||
      !std::isfinite(camera.far_plane) || camera.vertical_field_of_view_radians <= 0.0F ||
      camera.vertical_field_of_view_radians >= std::numbers::pi_v<float> ||
      camera.near_plane <= 0.0F || camera.far_plane <= camera.near_plane) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  const auto focal = 1.0F / std::tan(camera.vertical_field_of_view_radians * 0.5F);
  matrix4 result;
  result.values[matrix_index(0U, 0U)] = focal / aspect;
  result.values[matrix_index(1U, 1U)] = -focal;
  result.values[matrix_index(2U, 2U)] = camera.far_plane / (camera.near_plane - camera.far_plane);
  result.values[matrix_index(2U, 3U)] =
      (camera.far_plane * camera.near_plane) / (camera.near_plane - camera.far_plane);
  result.values[matrix_index(3U, 2U)] = -1.0F;
  out_matrix = result;
  return GNEISS_SUCCESS;
}

std::array<float, 4> transform_vector(const matrix4& matrix,
                                      const std::array<float, 4>& vector) noexcept {
  std::array<float, 4> result{};
  for (std::size_t row = 0; row < 4U; ++row) {
    for (std::size_t column = 0; column < 4U; ++column) {
      result[row] += matrix.values[matrix_index(row, column)] * vector[column];
    }
  }
  return result;
}

} // namespace gneiss::render_internal
