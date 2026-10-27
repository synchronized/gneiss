// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/aabb.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/type.hpp"
#include <cmath>
#include <limits>
#include <type_traits>

static_assert(std::is_same_v<gneiss::engine::math::vector3, gneiss::engine::math::vector3>);
static_assert(std::is_same_v<gneiss::engine::math::quaternion, gneiss::engine::math::quaternion>);

int main() {
  using namespace gneiss::engine;
  using namespace gneiss::engine::math;
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
  const auto matrix = model_matrix(parent);
  const auto elements = matrix_to_array(matrix);
  if (elements[12] != 10 || elements[13] != 20 || elements[14] != 30 ||
      matrix_from_array(elements) != matrix)
    return 3;
  // 同一仿射矩阵下，方向不受平移影响，位置会受影响。
  matrix4 affine{1.0F};
  affine[0][0] = 2.0F;
  affine[3] = vector4{10, 20, 30, 1};
  if (transform_vector(affine, vector3{1, 2, 3}) != vector3{2, 2, 3})
    return 5;
  vector3 point{7, 8, 9};
  if (!transform_point(affine, vector3{1, 2, 3}, point) || point != vector3{12, 22, 33})
    return 6;
  affine[3][3] = 2.0F;
  if (!transform_point(affine, vector3{1, 2, 3}, point) || point != vector3{6, 11, 16.5F})
    return 7;
  const auto preserved = point;
  affine[3][3] = 0;
  if (transform_point(affine, vector3{1, 2, 3}, point) || point != preserved)
    return 8;
  affine[3][3] = std::numeric_limits<float>::denorm_min();
  if (transform_point(affine, vector3{1, 2, 3}, point) || point != preserved)
    return 9;
  if (!is_finite(matrix3{1.0F}) || !is_finite(matrix4{1.0F}))
    return 10;
  matrix3 nonfinite3{1.0F};
  nonfinite3[2][1] = std::numeric_limits<float>::quiet_NaN();
  affine[1][2] = std::numeric_limits<float>::infinity();
  if (is_finite(nonfinite3) || is_finite(affine) ||
      transform_point(affine, vector3{1, 2, 3}, point) || point != preserved)
    return 11;
  const aabb box{{-2, -4, -6}, {2, 4, 6}};
  const aabb touching{{2, 4, 6}, {3, 5, 7}};
  const aabb separate{{3, 5, 7}, {4, 6, 8}};
  if (!box.is_valid() || box.center() != vector3{0} || box.size() != vector3{4, 8, 12} ||
      !box.contains(box.min) || !box.contains(box.max) || box.contains(vector3{3, 0, 0}) ||
      !box.intersects(touching) || !touching.intersects(box) || box.intersects(separate))
    return 12;
  const aabb point_box{};
  const auto enlarged = box.expanded({5, 0, -8});
  const auto combined = box.merged(separate);
  if (!point_box.is_valid() || !point_box.contains(vector3{0}) || !point_box.intersects(box) ||
      enlarged.min != vector3{-2, -4, -8} || enlarged.max != vector3{5, 4, 6} ||
      combined.min != box.min || combined.max != separate.max)
    return 13;
  const aabb reversed{{1, 0, 0}, {0, 1, 1}};
  const aabb nonfinite{{0, 0, 0}, {std::numeric_limits<float>::infinity(), 1, 1}};
  const vector3 nan_point{std::numeric_limits<float>::quiet_NaN(), 0, 0};
  if (reversed.is_valid() || nonfinite.is_valid() || reversed.contains(vector3{0}) ||
      box.intersects(reversed) || nonfinite.intersects(box) || box.contains(nan_point) ||
      aabb{nan_point, vector3{1}}.is_valid())
    return 14;
  const float largest = std::numeric_limits<float>::max();
  if (aabb{{largest, 0, 0}, {largest, 0, 0}}.center().x != largest ||
      aabb{{-largest, 0, 0}, {largest, 0, 0}}.center().x != 0)
    return 15;
  auto invalid = parent;
  invalid.rotation = quaternion_xyzw(0, 0, 0, 0);
  return is_valid_transform(parent) && !is_valid_transform(invalid) ? 0 : 4;
}
