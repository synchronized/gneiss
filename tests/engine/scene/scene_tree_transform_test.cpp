// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float half_sqrt_two = 0.7071067811865475F;

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
  auto root_result = scene->create_object();
  if (!root_result)
    return 1;
  const auto root = *root_result;
  auto child_result = scene->create_object({}, root);
  if (!child_result)
    return 1;
  const auto child = *child_result;
  auto grandchild_result = scene->create_object({}, child);
  auto other_result = scene->create_object();
  if (!grandchild_result || !other_result)
    return 1;
  const auto grandchild = *grandchild_result;
  const auto other_root = *other_result;
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
  // 固定夹具的逐级 TRS：子级合成位移为 (0, 1.5, 5)，根缩放并绕 Z 旋转后为 (5.5, 0, 20)。
  // 这里验证 world_transform 的分量组合，不替代包含剪切的 world_matrix 验证。
  const gneiss::math::transform expected{
      {5.5F, 0, 20}, {0, 0, half_sqrt_two, half_sqrt_two}, {1, 1.5F, 8}};
  if (!approximately_equal(grandchild.world_transform(), expected) ||
      grandchild.entity() != entity) {
    return 3;
  }

  gneiss::math::transform other_transform = gneiss::math::transform{};
  other_transform.translation[2] = -5.0F;
  other_transform.rotation[1] = -half_sqrt_two;
  other_transform.rotation[3] = half_sqrt_two;
  // 新父级绕 Y -90 度，与子级 +90 度抵消；位移变成 (-0.5, 1, -7)。
  const gneiss::math::transform reparented{{-0.5F, 1, -7}, {1, 0, 0, 0}, {1, 0.25F, 2}};
  if (other_root.set_local_transform(other_transform) != gneiss::error::success ||
      grandchild.reparent(other_root) != gneiss::error::success ||
      !approximately_equal(grandchild.world_transform(), reparented) ||
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
      !approximately_equal(grandchild.world_transform(), reparented)) {
    return 7;
  }
  // 先创建子对象再挂到后建的父对象，递归删除允许 ECS 移动组件存储。
  if (root.reparent(child) != gneiss::error::invalid_argument || root.reparent(other_root).failed())
    return 8;
  other_root.destroy();
  if (root.is_valid() || child.is_valid() || grandchild.is_valid())
    return 8;
  return 0;
}

} // namespace

int main() { return run_tests(); }
