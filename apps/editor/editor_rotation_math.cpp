// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_rotation_math.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gneiss::editor {
namespace {

constexpr float normalization_tolerance = 1.0e-3F;

bool is_finite(const gneiss_property_quaternion& value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
         std::isfinite(value.w);
}

} // namespace

result quaternion_to_euler_degrees(const gneiss_property_quaternion& quaternion,
                                   std::array<float, 3>& output) noexcept {
  if (!is_finite(quaternion)) {
    return result::invalid_argument;
  }
  const auto length = std::sqrt((quaternion.x * quaternion.x) + (quaternion.y * quaternion.y) +
                                (quaternion.z * quaternion.z) + (quaternion.w * quaternion.w));
  if (length <= normalization_tolerance) {
    return result::invalid_argument;
  }
  const auto x = quaternion.x / length;
  const auto y = quaternion.y / length;
  const auto z = quaternion.z / length;
  const auto w = quaternion.w / length;

  const auto sin_x = 2.0F * ((w * x) + (y * z));
  const auto cos_x = 1.0F - (2.0F * ((x * x) + (y * y)));
  const auto sin_y = std::clamp(2.0F * ((w * y) - (z * x)), -1.0F, 1.0F);
  const auto sin_z = 2.0F * ((w * z) + (x * y));
  const auto cos_z = 1.0F - (2.0F * ((y * y) + (z * z)));
  constexpr auto radians_to_degrees = 180.0F / std::numbers::pi_v<float>;
  output = {std::atan2(sin_x, cos_x) * radians_to_degrees, std::asin(sin_y) * radians_to_degrees,
            std::atan2(sin_z, cos_z) * radians_to_degrees};
  return result::success;
}

result euler_degrees_to_quaternion(const std::array<float, 3>& euler,
                                   gneiss_property_quaternion& output) noexcept {
  if (!std::ranges::all_of(euler, [](float value) { return std::isfinite(value); })) {
    return result::invalid_argument;
  }
  constexpr auto half_degrees_to_radians = std::numbers::pi_v<float> / 360.0F;
  const auto x = euler[0] * half_degrees_to_radians;
  const auto y = euler[1] * half_degrees_to_radians;
  const auto z = euler[2] * half_degrees_to_radians;
  const auto cx = std::cos(x);
  const auto sx = std::sin(x);
  const auto cy = std::cos(y);
  const auto sy = std::sin(y);
  const auto cz = std::cos(z);
  const auto sz = std::sin(z);
  output = {.x = (sx * cy * cz) - (cx * sy * sz),
            .y = (cx * sy * cz) + (sx * cy * sz),
            .z = (cx * cy * sz) - (sx * sy * cz),
            .w = (cx * cy * cz) + (sx * sy * sz)};
  return result::success;
}

} // namespace gneiss::editor
