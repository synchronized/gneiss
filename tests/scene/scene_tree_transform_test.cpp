// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/scene_tree.h"

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

gneiss_transform reference_combine(const gneiss_transform& parent,
                                   const gneiss_transform& local) noexcept {
  gneiss_transform result = GNEISS_TRANSFORM_IDENTITY;
  std::array scaled_translation{local.translation[0] * parent.scale[0],
                                local.translation[1] * parent.scale[1],
                                local.translation[2] * parent.scale[2]};
  scaled_translation = rotate(parent.rotation, scaled_translation);
  for (std::size_t index = 0; index < scaled_translation.size(); ++index) {
    result.translation[index] = parent.translation[index] + scaled_translation[index];
    result.scale[index] = parent.scale[index] * local.scale[index];
  }
  const auto* left = parent.rotation;
  const auto* right = local.rotation;
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

bool approximately_equal(const gneiss_transform& left, const gneiss_transform& right) noexcept {
  const auto close = [](float first, float second) { return std::abs(first - second) < 1.0e-5F; };
  return std::equal(std::begin(left.translation), std::end(left.translation),
                    std::begin(right.translation), close) &&
         std::equal(std::begin(left.rotation), std::end(left.rotation), std::begin(right.rotation),
                    close) &&
         std::equal(std::begin(left.scale), std::end(left.scale), std::begin(right.scale), close);
}

int run_tests() {
  gneiss::scene_internal::scene_tree tree{17};
  gneiss_scene_node_id root{};
  gneiss_scene_node_id child{};
  gneiss_scene_node_id grandchild{};
  gneiss_scene_node_id other_root{};
  constexpr gneiss_entity_id entity = 42;
  if (tree.create({}, {}, &root) != GNEISS_SUCCESS ||
      tree.create(root, {}, &child) != GNEISS_SUCCESS ||
      tree.create(child, entity, &grandchild) != GNEISS_SUCCESS ||
      tree.create({}, {}, &other_root) != GNEISS_SUCCESS) {
    return 1;
  }

  gneiss_transform root_transform = GNEISS_TRANSFORM_IDENTITY;
  root_transform.translation[0] = 10.0F;
  root_transform.rotation[2] = half_sqrt_two;
  root_transform.rotation[3] = half_sqrt_two;
  root_transform.scale[0] = 2.0F;
  root_transform.scale[1] = 3.0F;
  root_transform.scale[2] = 4.0F;
  gneiss_transform child_transform = GNEISS_TRANSFORM_IDENTITY;
  child_transform.translation[0] = 1.0F;
  child_transform.translation[1] = 2.0F;
  child_transform.translation[2] = 3.0F;
  child_transform.rotation[0] = half_sqrt_two;
  child_transform.rotation[3] = half_sqrt_two;
  child_transform.scale[0] = 0.5F;
  child_transform.scale[1] = 2.0F;
  gneiss_transform grandchild_transform = GNEISS_TRANSFORM_IDENTITY;
  grandchild_transform.translation[0] = -2.0F;
  grandchild_transform.translation[1] = 1.0F;
  grandchild_transform.translation[2] = 0.5F;
  grandchild_transform.rotation[1] = half_sqrt_two;
  grandchild_transform.rotation[3] = half_sqrt_two;
  grandchild_transform.scale[1] = 0.25F;
  grandchild_transform.scale[2] = 2.0F;
  if (tree.set_local(root, root_transform) != GNEISS_SUCCESS ||
      tree.set_local(child, child_transform) != GNEISS_SUCCESS ||
      tree.set_local(grandchild, grandchild_transform) != GNEISS_SUCCESS) {
    return 2;
  }
  const auto expected =
      reference_combine(root_transform, reference_combine(child_transform, grandchild_transform));
  gneiss_transform actual = GNEISS_TRANSFORM_IDENTITY;
  if (tree.get_world(grandchild, &actual) != GNEISS_SUCCESS ||
      !approximately_equal(actual, expected) || tree.get_entity(grandchild) != entity) {
    return 3;
  }

  gneiss_transform other_transform = GNEISS_TRANSFORM_IDENTITY;
  other_transform.translation[2] = -5.0F;
  other_transform.rotation[1] = -half_sqrt_two;
  other_transform.rotation[3] = half_sqrt_two;
  if (tree.set_local(other_root, other_transform) != GNEISS_SUCCESS ||
      tree.reparent(grandchild, other_root) != GNEISS_SUCCESS ||
      tree.get_world(grandchild, &actual) != GNEISS_SUCCESS ||
      !approximately_equal(actual, reference_combine(other_transform, grandchild_transform)) ||
      tree.get_entity(grandchild) != entity) {
    return 4;
  }

  auto invalid = grandchild_transform;
  invalid.translation[0] = std::numeric_limits<float>::infinity();
  if (tree.set_local(grandchild, invalid) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 5;
  }
  invalid = grandchild_transform;
  invalid.scale[1] = 0.0F;
  if (tree.set_local(grandchild, invalid) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 6;
  }
  invalid = grandchild_transform;
  invalid.rotation[3] = 2.0F;
  if (tree.set_local(grandchild, invalid) != GNEISS_ERROR_INVALID_ARGUMENT ||
      tree.get_world(grandchild, &actual) != GNEISS_SUCCESS ||
      !approximately_equal(actual, reference_combine(other_transform, grandchild_transform))) {
    return 7;
  }
  return 0;
}

} // namespace

int main() { return run_tests(); }
