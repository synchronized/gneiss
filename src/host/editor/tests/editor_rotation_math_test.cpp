// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "viewport/editor_rotation_math.hpp"

#include <array>
#include <cmath>

namespace {

bool near(float left, float right, float tolerance = 1.0e-3F) noexcept {
  return std::abs(left - right) <= tolerance;
}

bool equivalent(const gneiss::property_quaternion& left,
                const gneiss::property_quaternion& right) noexcept {
  const auto dot =
      (left.x * right.x) + (left.y * right.y) + (left.z * right.z) + (left.w * right.w);
  return near(std::abs(dot), 1.0F);
}

} // namespace

int main() {
  constexpr std::array<float, 3> source{35.0F, -20.0F, 125.0F};
  gneiss::property_quaternion quaternion{};
  if (gneiss::editor::euler_degrees_to_quaternion(source, quaternion) !=
      gneiss::engine::result::success) {
    return 1;
  }
  std::array<float, 3> recovered{};
  if (gneiss::editor::quaternion_to_euler_degrees(quaternion, recovered) !=
          gneiss::engine::result::success ||
      !near(recovered[0], source[0]) || !near(recovered[1], source[1]) ||
      !near(recovered[2], source[2])) {
    return 2;
  }

  constexpr std::array<float, 3> near_gimbal{15.0F, 89.9F, -40.0F};
  gneiss::property_quaternion gimbal_quaternion{};
  std::array<float, 3> gimbal_recovered{};
  gneiss::property_quaternion gimbal_roundtrip{};
  if (gneiss::editor::euler_degrees_to_quaternion(near_gimbal, gimbal_quaternion) !=
          gneiss::engine::result::success ||
      gneiss::editor::quaternion_to_euler_degrees(gimbal_quaternion, gimbal_recovered) !=
          gneiss::engine::result::success ||
      gneiss::editor::euler_degrees_to_quaternion(gimbal_recovered, gimbal_roundtrip) !=
          gneiss::engine::result::success ||
      !equivalent(gimbal_quaternion, gimbal_roundtrip)) {
    return 3;
  }

  constexpr gneiss::property_quaternion invalid{.w = 0.0F};
  if (gneiss::editor::quaternion_to_euler_degrees(invalid, recovered) !=
      gneiss::engine::result::invalid_argument) {
    return 4;
  }
  return 0;
}
