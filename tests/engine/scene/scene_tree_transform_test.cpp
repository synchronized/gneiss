// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr float half_sqrt_two = 0.7071067811865475F;

std::array<float, 3> rotate(const float* quaternion, std::array<float, 3> point) noexcept {
  const std::array vector{quaternion[0], quaternion[1], quaternion[2]};
  const std::array first_cross{(vector[1] * point[2]) - (vector[2] * point[1]),
                               (vector[2] * point[0]) - (vector[0] * point[2]),
                               (vector[0] * point[1]) - (vector[1] * point[0])};
  const std::array second_cross{(vector[1] * first_cross[2]) - (vector[2] * first_cross[1]),
                                (vector[2] * first_cross[0]) - (vector[0] * first_cross[2]),
                                (vector[0] * first_cross[1]) - (vector[1] * first_cross[0])};
  for (std::size_t index = 0; index < point.size(); ++index) {
    point[index] += 2.0F * ((quaternion[3] * first_cross[index]) + second_cross[index]);
  }
  return point;
}

gneiss::math::transform reference_combine(const gneiss::math::transform& parent,
                                          const gneiss::math::transform& local) noexcept {
  gneiss::math::transform result = gneiss::math::transform{};
  std::array scaled_translation{local.translation[0] * parent.scale[0],
                                local.translation[1] * parent.scale[1],
                                local.translation[2] * parent.scale[2]};
  scaled_translation = rotate(gneiss::math::components(parent.rotation).data(), scaled_translation);
  for (std::size_t index = 0; index < scaled_translation.size(); ++index) {
    result.translation[index] = parent.translation[index] + scaled_translation[index];
    result.scale[index] = parent.scale[index] * local.scale[index];
  }
  const auto& left = parent.rotation;
  const auto& right = local.rotation;
  result.rotation[0] =
      (left[3] * right[0]) + (left[0] * right[3]) + (left[1] * right[2]) - (left[2] * right[1]);
  result.rotation[1] =
      (left[3] * right[1]) - (left[0] * right[2]) + (left[1] * right[3]) + (left[2] * right[0]);
  result.rotation[2] =
      (left[3] * right[2]) + (left[0] * right[1]) - (left[1] * right[0]) + (left[2] * right[3]);
  result.rotation[3] =
      (left[3] * right[3]) - (left[0] * right[0]) - (left[1] * right[1]) - (left[2] * right[2]);
  return result;
}

bool approximately_equal(const gneiss::math::transform& left,
                         const gneiss::math::transform& right) noexcept {
  const auto close = [](float first, float second) { return std::abs(first - second) < 1.0e-5F; };
  return std::equal(gneiss::math::components(left.translation).begin(),
                    gneiss::math::components(left.translation).end(),
                    gneiss::math::components(right.translation).begin(), close) &&
         std::equal(gneiss::math::components(left.rotation).begin(),
                    gneiss::math::components(left.rotation).end(),
                    gneiss::math::components(right.rotation).begin(), close) &&
         std::equal(gneiss::math::components(left.scale).begin(),
                    gneiss::math::components(left.scale).end(),
                    gneiss::math::components(right.scale).begin(), close);
}

int run_tests() {
  if (gneiss::initialize_meta().failed())
    return 1;
  auto owner = gneiss::create_native();
  if (!owner)
    return 1;
  auto scene = (*owner)->create_scene();
  if (!scene)
    return 1;
  gneiss::game_object root, child, grandchild, other_root;
  if (gneiss::test_support::assign_value(scene->create_object(), root).failed() ||
      gneiss::test_support::assign_value(scene->create_object({}, root), child).failed() ||
      gneiss::test_support::assign_value(scene->create_object({}, child), grandchild).failed() ||
      gneiss::test_support::assign_value(scene->create_object(), other_root).failed())
    return 1;
  const auto entity = grandchild.entity();

  gneiss::math::transform root_transform = gneiss::math::transform{};
  root_transform.translation[0] = 10.0F;
  root_transform.rotation[2] = half_sqrt_two;
  root_transform.rotation[3] = half_sqrt_two;
  root_transform.scale[0] = 2.0F;
  root_transform.scale[1] = 3.0F;
  root_transform.scale[2] = 4.0F;
  gneiss::math::transform child_transform = gneiss::math::transform{};
  child_transform.translation[0] = 1.0F;
  child_transform.translation[1] = 2.0F;
  child_transform.translation[2] = 3.0F;
  child_transform.rotation[0] = half_sqrt_two;
  child_transform.rotation[3] = half_sqrt_two;
  child_transform.scale[0] = 0.5F;
  child_transform.scale[1] = 2.0F;
  gneiss::math::transform grandchild_transform = gneiss::math::transform{};
  grandchild_transform.translation[0] = -2.0F;
  grandchild_transform.translation[1] = 1.0F;
  grandchild_transform.translation[2] = 0.5F;
  grandchild_transform.rotation[1] = half_sqrt_two;
  grandchild_transform.rotation[3] = half_sqrt_two;
  grandchild_transform.scale[1] = 0.25F;
  grandchild_transform.scale[2] = 2.0F;
  if (root.set_local_transform(root_transform) != gneiss::error::success ||
      child.set_local_transform(child_transform) != gneiss::error::success ||
      grandchild.set_local_transform(grandchild_transform) != gneiss::error::success) {
    return 2;
  }
  const auto expected =
      reference_combine(root_transform, reference_combine(child_transform, grandchild_transform));
  if (!approximately_equal(grandchild.world_transform(), expected) ||
      grandchild.entity() != entity) {
    return 3;
  }

  gneiss::math::transform other_transform = gneiss::math::transform{};
  other_transform.translation[2] = -5.0F;
  other_transform.rotation[1] = -half_sqrt_two;
  other_transform.rotation[3] = half_sqrt_two;
  if (other_root.set_local_transform(other_transform) != gneiss::error::success ||
      grandchild.reparent(other_root) != gneiss::error::success ||
      !approximately_equal(grandchild.world_transform(),
                           reference_combine(other_transform, grandchild_transform)) ||
      grandchild.entity() != entity) {
    return 4;
  }

  auto invalid = grandchild_transform;
  invalid.translation[0] = std::numeric_limits<float>::infinity();
  if (grandchild.set_local_transform(invalid) != gneiss::error::invalid_argument) {
    return 5;
  }
  invalid = grandchild_transform;
  invalid.scale[1] = 0.0F;
  if (grandchild.set_local_transform(invalid) != gneiss::error::invalid_argument) {
    return 6;
  }
  invalid = grandchild_transform;
  invalid.rotation[3] = 2.0F;
  if (grandchild.set_local_transform(invalid) != gneiss::error::invalid_argument ||
      !approximately_equal(grandchild.world_transform(),
                           reference_combine(other_transform, grandchild_transform))) {
    return 7;
  }
  // 先创建子对象再挂到后建的父对象，递归删除允许 ECS 移动组件存储。
  if (root.reparent(child) != gneiss::error::invalid_argument ||
      root.reparent(other_root).failed())
    return 8;
  other_root.destroy();
  if (root.is_valid() || child.is_valid() || grandchild.is_valid())
    return 8;
  return 0;
}

} // namespace

int main() { return run_tests(); }
