// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/object_uniform.h"

#include <cmath>
#include <limits>

namespace gneiss::application_internal {
namespace {

constexpr std::size_t matrix_index(std::size_t row, std::size_t column) noexcept {
  return (column * 4U) + row;
}

render_internal::matrix4 multiply(const render_internal::matrix4& left,
                                  const render_internal::matrix4& right) noexcept {
  render_internal::matrix4 result;
  for (std::size_t row = 0; row < 4U; ++row) {
    for (std::size_t column = 0; column < 4U; ++column) {
      for (std::size_t inner = 0; inner < 4U; ++inner) {
        result.values[matrix_index(row, column)] +=
            left.values[matrix_index(row, inner)] * right.values[matrix_index(inner, column)];
      }
    }
  }
  return result;
}

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

} // namespace

bool calculate_uniform_stride(std::uint64_t alignment, std::uint64_t& stride) noexcept {
  if (alignment == 0U) {
    return false;
  }
  constexpr auto size = static_cast<std::uint64_t>(sizeof(object_uniform));
  const auto remainder = size % alignment;
  const auto padding = remainder == 0U ? 0U : alignment - remainder;
  if (size > std::numeric_limits<std::uint64_t>::max() - padding) {
    return false;
  }
  stride = size + padding;
  return stride <= std::numeric_limits<std::uint32_t>::max();
}

bool build_object_uniform(const render_internal::matrix4& view,
                          const render_internal::matrix4& projection,
                          const gneiss_transform& transform, const std::array<float, 4>& color,
                          object_uniform& output) noexcept {
  render_internal::matrix4 model;
  render_internal::matrix4 normal;
  if (!build_model_matrices(transform, model, normal)) {
    return false;
  }
  for (const auto value : color) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  output = {.model_view_projection = multiply(projection, multiply(view, model)),
            .normal = normal,
            .color = color};
  return true;
}

} // namespace gneiss::application_internal
