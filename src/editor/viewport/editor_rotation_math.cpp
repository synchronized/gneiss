// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "viewport/editor_rotation_math.hpp"
#include "engine/core/result.hpp"

#include <algorithm>
#include <cmath>

namespace gneiss::editor {
namespace {

constexpr float normalization_tolerance = 1.0e-3F;

bool is_finite(const gneiss::property_quaternion& value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
         std::isfinite(value.w);
}

} // namespace

gneiss::engine::result quaternion_to_euler_degrees(const gneiss::property_quaternion& quaternion,
                                                   std::array<float, 3>& output) noexcept {
  if (!is_finite(quaternion)) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto length = glm::length(quaternion);
  if (!std::isfinite(length) || length <= normalization_tolerance) {
    return gneiss::engine::result::invalid_argument;
  }
  output = gneiss::engine::component_array(glm::degrees(glm::eulerAngles(quaternion / length)));
  return gneiss::engine::result::success;
}

gneiss::engine::result euler_degrees_to_quaternion(const std::array<float, 3>& euler,
                                                   gneiss::property_quaternion& output) noexcept {
  if (!std::ranges::all_of(euler, [](float value) { return std::isfinite(value); })) {
    return gneiss::engine::result::invalid_argument;
  }
  output = glm::quat(glm::radians(gneiss::engine::vector3{euler[0], euler[1], euler[2]}));
  return gneiss::engine::result::success;
}

} // namespace gneiss::editor
