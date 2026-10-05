// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/backend/granit/scene_projection_math.hpp"

#include <array>
#include <cmath>

namespace gneiss::render_internal {
namespace {

constexpr std::size_t matrix_index(std::size_t row, std::size_t column) noexcept {
  return (column * 4U) + row;
}

} // namespace

bool build_model_matrices(const gneiss_transform& transform, render_internal::matrix4& model,
                          render_internal::matrix4& normal) noexcept {
  const auto x = transform.rotation[0];
  const auto y = transform.rotation[1];
  const auto z = transform.rotation[2];
  const auto w = transform.rotation[3];
  const auto length_squared = (x * x) + (y * y) + (z * z) + (w * w);
  if (!std::isfinite(length_squared) || length_squared <= 0.0F) {
    return false;
  }
  for (const auto value : transform.translation) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  for (const auto value : transform.scale) {
    if (!std::isfinite(value) || value == 0.0F) {
      return false;
    }
  }

  const auto inverse_length = 1.0F / std::sqrt(length_squared);
  const auto qx = x * inverse_length;
  const auto qy = y * inverse_length;
  const auto qz = z * inverse_length;
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
  const std::array rotation{
      1.0F - (2.0F * (yy + zz)), 2.0F * (xy + wz),          2.0F * (xz - wy),
      2.0F * (xy - wz),          1.0F - (2.0F * (xx + zz)), 2.0F * (yz + wx),
      2.0F * (xz + wy),          2.0F * (yz - wx),          1.0F - (2.0F * (xx + yy))};
  for (std::size_t column = 0; column < 3U; ++column) {
    for (std::size_t row = 0; row < 3U; ++row) {
      const auto rotation_value = rotation[(column * 3U) + row];
      model.values[matrix_index(row, column)] = rotation_value * transform.scale[column];
      normal.values[matrix_index(row, column)] = rotation_value / transform.scale[column];
    }
    model.values[matrix_index(column, 3U)] = transform.translation[column];
  }
  model.values[matrix_index(3U, 3U)] = 1.0F;
  normal.values[matrix_index(3U, 3U)] = 1.0F;
  return true;
}

} // namespace gneiss::render_internal
