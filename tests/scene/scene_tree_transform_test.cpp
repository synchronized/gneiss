// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_tree.hpp"

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

gneiss::engine::transform reference_combine(const gneiss::engine::transform& parent,
                                            const gneiss::engine::transform& local) noexcept {
  gneiss::engine::transform result = gneiss::engine::transform{};
  std::array scaled_translation{local.translation[0] * parent.scale[0],
                                local.translation[1] * parent.scale[1],
                                local.translation[2] * parent.scale[2]};
  scaled_translation =
      rotate(gneiss::engine::components(parent.rotation).data(), scaled_translation);
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

bool approximately_equal(const gneiss::engine::transform& left,
                         const gneiss::engine::transform& right) noexcept {
  const auto close = [](float first, float second) { return std::abs(first - second) < 1.0e-5F; };
  return std::equal(gneiss::engine::components(left.translation).begin(),
                    gneiss::engine::components(left.translation).end(),
                    gneiss::engine::components(right.translation).begin(), close) &&
         std::equal(gneiss::engine::components(left.rotation).begin(),
                    gneiss::engine::components(left.rotation).end(),
                    gneiss::engine::components(right.rotation).begin(), close) &&
         std::equal(gneiss::engine::components(left.scale).begin(),
                    gneiss::engine::components(left.scale).end(),
                    gneiss::engine::components(right.scale).begin(), close);
}

int run_tests() {
  gneiss::engine::scene_tree tree{17};
  gneiss::scene_node_id root{};
  gneiss::scene_node_id child{};
  gneiss::scene_node_id grandchild{};
  gneiss::scene_node_id other_root{};
  constexpr std::uint64_t entity = 42;
  if (tree.create({}, {}, &root) != gneiss::engine::result::success ||
      tree.create(root, {}, &child) != gneiss::engine::result::success ||
      tree.create(child, entity, &grandchild) != gneiss::engine::result::success ||
      tree.create({}, {}, &other_root) != gneiss::engine::result::success) {
    return 1;
  }

  gneiss::engine::transform root_transform = gneiss::engine::transform{};
  root_transform.translation[0] = 10.0F;
  root_transform.rotation[2] = half_sqrt_two;
  root_transform.rotation[3] = half_sqrt_two;
  root_transform.scale[0] = 2.0F;
  root_transform.scale[1] = 3.0F;
  root_transform.scale[2] = 4.0F;
  gneiss::engine::transform child_transform = gneiss::engine::transform{};
  child_transform.translation[0] = 1.0F;
  child_transform.translation[1] = 2.0F;
  child_transform.translation[2] = 3.0F;
  child_transform.rotation[0] = half_sqrt_two;
  child_transform.rotation[3] = half_sqrt_two;
  child_transform.scale[0] = 0.5F;
  child_transform.scale[1] = 2.0F;
  gneiss::engine::transform grandchild_transform = gneiss::engine::transform{};
  grandchild_transform.translation[0] = -2.0F;
  grandchild_transform.translation[1] = 1.0F;
  grandchild_transform.translation[2] = 0.5F;
  grandchild_transform.rotation[1] = half_sqrt_two;
  grandchild_transform.rotation[3] = half_sqrt_two;
  grandchild_transform.scale[1] = 0.25F;
  grandchild_transform.scale[2] = 2.0F;
  if (tree.set_local(root, root_transform) != gneiss::engine::result::success ||
      tree.set_local(child, child_transform) != gneiss::engine::result::success ||
      tree.set_local(grandchild, grandchild_transform) != gneiss::engine::result::success) {
    return 2;
  }
  const auto expected =
      reference_combine(root_transform, reference_combine(child_transform, grandchild_transform));
  gneiss::engine::transform actual = gneiss::engine::transform{};
  if (tree.get_world(grandchild, &actual) != gneiss::engine::result::success ||
      !approximately_equal(actual, expected) || tree.get_entity(grandchild) != entity) {
    return 3;
  }

  gneiss::engine::transform other_transform = gneiss::engine::transform{};
  other_transform.translation[2] = -5.0F;
  other_transform.rotation[1] = -half_sqrt_two;
  other_transform.rotation[3] = half_sqrt_two;
  if (tree.set_local(other_root, other_transform) != gneiss::engine::result::success ||
      tree.reparent(grandchild, other_root) != gneiss::engine::result::success ||
      tree.get_world(grandchild, &actual) != gneiss::engine::result::success ||
      !approximately_equal(actual, reference_combine(other_transform, grandchild_transform)) ||
      tree.get_entity(grandchild) != entity) {
    return 4;
  }

  auto invalid = grandchild_transform;
  invalid.translation[0] = std::numeric_limits<float>::infinity();
  if (tree.set_local(grandchild, invalid) != gneiss::engine::result::invalid_argument) {
    return 5;
  }
  invalid = grandchild_transform;
  invalid.scale[1] = 0.0F;
  if (tree.set_local(grandchild, invalid) != gneiss::engine::result::invalid_argument) {
    return 6;
  }
  invalid = grandchild_transform;
  invalid.rotation[3] = 2.0F;
  if (tree.set_local(grandchild, invalid) != gneiss::engine::result::invalid_argument ||
      tree.get_world(grandchild, &actual) != gneiss::engine::result::success ||
      !approximately_equal(actual, reference_combine(other_transform, grandchild_transform))) {
    return 7;
  }
  return 0;
}

} // namespace

int main() { return run_tests(); }
