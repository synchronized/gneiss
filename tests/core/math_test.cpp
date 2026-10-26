// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/transform.hpp"
#include "engine/core/reflection/reflection_types.hpp"
#include <cmath>
#include <type_traits>

static_assert(std::is_same_v<gneiss::property_vec3, gneiss::engine::vector3>);
static_assert(std::is_same_v<gneiss::property_quaternion, gneiss::engine::quaternion>);

int main() {
  using namespace gneiss::engine;
  const auto near = [](float a, float b) { return std::abs(a - b) < 1.0e-5F; };
  // 非平凡旋转同时约束构造顺序、文件分量顺序和右手旋转方向。
  constexpr float half_sqrt_two = 0.7071067811865475F;
  const auto rotation = quaternion_xyzw(0, 0, half_sqrt_two, half_sqrt_two);
  const auto stored = component_array(rotation);
  const auto rotated = rotation * vector3{1, 0, 0};
  if (stored[0] != 0 || stored[1] != 0 || stored[2] != half_sqrt_two ||
      stored[3] != half_sqrt_two || !near(rotated.x, 0) || !near(rotated.y, 1))
    return 1;
  const transform parent{{10, 20, 30}, rotation, {2, 3, 4}};
  const transform local{{1, 0, 0}, quaternion{1, 0, 0, 0}, {1, 1, 1}};
  const auto world = compose_transform(parent, local);
  if (!near(world.translation.x, 10) || !near(world.translation.y, 22) ||
      !near(world.translation.z, 30))
    return 2;
  const auto matrix = transform_matrix(parent);
  const auto elements = matrix_to_array(matrix);
  if (elements[12] != 10 || elements[13] != 20 || elements[14] != 30 ||
      matrix_from_array(elements) != matrix)
    return 3;
  auto invalid = parent;
  invalid.rotation = quaternion_xyzw(0, 0, 0, 0);
  return valid_transform(parent) && !valid_transform(invalid) ? 0 : 4;
}
