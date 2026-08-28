// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace gneiss::editor {
namespace {

constexpr std::size_t matrix_index(std::size_t row, std::size_t column) noexcept {
  return (column * 4U) + row;
}

[[nodiscard]] bool valid(const transform& value) noexcept {
  const auto finite = [](float component) { return std::isfinite(component); };
  const auto length = std::sqrt(std::inner_product(
      std::begin(value.rotation), std::end(value.rotation), std::begin(value.rotation), 0.0F));
  return std::ranges::all_of(value.translation, finite) &&
         std::ranges::all_of(value.rotation, finite) && std::ranges::all_of(value.scale, finite) &&
         std::abs(length - 1.0F) <= 1.0e-4F &&
         std::ranges::all_of(value.scale,
                             [](float component) { return std::abs(component) >= 1.0e-6F; });
}

[[nodiscard]] std::array<float, 3> rotate_inverse(const float* rotation,
                                                  std::array<float, 3> value) noexcept {
  const std::array inverse{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
  const std::array uv = {(inverse[1] * value[2]) - (inverse[2] * value[1]),
                         (inverse[2] * value[0]) - (inverse[0] * value[2]),
                         (inverse[0] * value[1]) - (inverse[1] * value[0])};
  const std::array uuv = {(inverse[1] * uv[2]) - (inverse[2] * uv[1]),
                          (inverse[2] * uv[0]) - (inverse[0] * uv[2]),
                          (inverse[0] * uv[1]) - (inverse[1] * uv[0])};
  for (std::size_t index = 0; index < value.size(); ++index) {
    value[index] += 2.0F * ((inverse[3] * uv[index]) + uuv[index]);
  }
  return value;
}

} // namespace

result transform_to_gizmo_matrix(const transform& value, gizmo_matrix& output) noexcept {
  if (!valid(value) || std::ranges::any_of(value.scale, [](float component) {
        return component < 0.0F;
      })) {
    return result::unsupported;
  }
  const auto x = value.rotation[0];
  const auto y = value.rotation[1];
  const auto z = value.rotation[2];
  const auto w = value.rotation[3];
  const std::array rotation{
      1.0F - (2.0F * ((y * y) + (z * z))), 2.0F * ((x * y) + (w * z)),
      2.0F * ((x * z) - (w * y)), 2.0F * ((x * y) - (w * z)),
      1.0F - (2.0F * ((x * x) + (z * z))), 2.0F * ((y * z) + (w * x)),
      2.0F * ((x * z) + (w * y)), 2.0F * ((y * z) - (w * x)),
      1.0F - (2.0F * ((x * x) + (y * y)))};
  output.fill(0.0F);
  for (std::size_t column = 0; column < 3U; ++column) {
    for (std::size_t row = 0; row < 3U; ++row) {
      output[matrix_index(row, column)] = rotation[(column * 3U) + row] * value.scale[column];
    }
    output[matrix_index(column, 3U)] = value.translation[column];
  }
  output[matrix_index(3U, 3U)] = 1.0F;
  return result::success;
}

result gizmo_matrix_to_transform(const gizmo_matrix& value, transform& output) noexcept {
  if (!std::ranges::all_of(value, [](float component) { return std::isfinite(component); }) ||
      std::abs(value[matrix_index(3U, 3U)] - 1.0F) > 1.0e-4F) {
    return result::invalid_argument;
  }
  transform result = GNEISS_TRANSFORM_IDENTITY;
  for (std::size_t column = 0; column < 3U; ++column) {
    const auto x = value[matrix_index(0U, column)];
    const auto y = value[matrix_index(1U, column)];
    const auto z = value[matrix_index(2U, column)];
    result.scale[column] = std::sqrt((x * x) + (y * y) + (z * z));
    result.translation[column] = value[matrix_index(column, 3U)];
    if (result.scale[column] < 1.0e-6F) {
      return result::unsupported;
    }
  }
  std::array<float, 9> rotation{};
  for (std::size_t column = 0; column < 3U; ++column) {
    for (std::size_t row = 0; row < 3U; ++row) {
      rotation[(column * 3U) + row] =
          value[matrix_index(row, column)] / result.scale[column];
    }
  }
  const auto determinant =
      (rotation[0] * ((rotation[4] * rotation[8]) - (rotation[7] * rotation[5]))) -
      (rotation[3] * ((rotation[1] * rotation[8]) - (rotation[7] * rotation[2]))) +
      (rotation[6] * ((rotation[1] * rotation[5]) - (rotation[4] * rotation[2])));
  if (determinant <= 0.0F) {
    return result::unsupported;
  }
  const auto dot01 = (rotation[0] * rotation[3]) + (rotation[1] * rotation[4]) +
                     (rotation[2] * rotation[5]);
  const auto dot02 = (rotation[0] * rotation[6]) + (rotation[1] * rotation[7]) +
                     (rotation[2] * rotation[8]);
  const auto dot12 = (rotation[3] * rotation[6]) + (rotation[4] * rotation[7]) +
                     (rotation[5] * rotation[8]);
  if (std::max({std::abs(dot01), std::abs(dot02), std::abs(dot12)}) > 1.0e-3F) {
    return result::unsupported;
  }
  const auto trace = rotation[0] + rotation[4] + rotation[8];
  if (trace > 0.0F) {
    const auto factor = 2.0F * std::sqrt(trace + 1.0F);
    result.rotation[3] = 0.25F * factor;
    result.rotation[0] = (rotation[5] - rotation[7]) / factor;
    result.rotation[1] = (rotation[6] - rotation[2]) / factor;
    result.rotation[2] = (rotation[1] - rotation[3]) / factor;
  } else {
    const std::size_t axis = rotation[4] > rotation[0] ? (rotation[8] > rotation[4] ? 2U : 1U)
                                                        : (rotation[8] > rotation[0] ? 2U : 0U);
    const auto next = (axis + 1U) % 3U;
    const auto last = (axis + 2U) % 3U;
    const auto factor =
        2.0F * std::sqrt(1.0F + rotation[(axis * 3U) + axis] -
                         rotation[(next * 3U) + next] - rotation[(last * 3U) + last]);
    result.rotation[axis] = 0.25F * factor;
    result.rotation[3] =
        (rotation[(next * 3U) + last] - rotation[(last * 3U) + next]) / factor;
    result.rotation[next] =
        (rotation[(axis * 3U) + next] + rotation[(next * 3U) + axis]) / factor;
    result.rotation[last] =
        (rotation[(axis * 3U) + last] + rotation[(last * 3U) + axis]) / factor;
  }
  const auto length = std::sqrt(std::inner_product(std::begin(result.rotation),
                                                   std::end(result.rotation),
                                                   std::begin(result.rotation), 0.0F));
  for (auto& component : result.rotation) {
    component /= length;
  }
  output = result;
  return result::success;
}

result world_to_local_transform(const transform* parent_world, const transform& target_world,
                                transform& output) noexcept {
  if (!valid(target_world) || (parent_world != nullptr && !valid(*parent_world))) {
    return result::invalid_argument;
  }
  if (std::ranges::any_of(target_world.scale, [](float value) { return value < 0.0F; }) ||
      (parent_world != nullptr &&
       std::ranges::any_of(parent_world->scale, [](float value) { return value < 0.0F; }))) {
    return result::unsupported;
  }
  if (parent_world == nullptr) {
    output = target_world;
    return result::success;
  }

  transform local = GNEISS_TRANSFORM_IDENTITY;
  std::array delta{target_world.translation[0] - parent_world->translation[0],
                   target_world.translation[1] - parent_world->translation[1],
                   target_world.translation[2] - parent_world->translation[2]};
  delta = rotate_inverse(parent_world->rotation, delta);
  for (std::size_t index = 0; index < delta.size(); ++index) {
    local.translation[index] = delta[index] / parent_world->scale[index];
    local.scale[index] = target_world.scale[index] / parent_world->scale[index];
  }
  const auto* parent = parent_world->rotation;
  const auto* world = target_world.rotation;
  local.rotation[0] = (parent[3] * world[0]) - (parent[0] * world[3]) - (parent[1] * world[2]) +
                      (parent[2] * world[1]);
  local.rotation[1] = (parent[3] * world[1]) + (parent[0] * world[2]) - (parent[1] * world[3]) -
                      (parent[2] * world[0]);
  local.rotation[2] = (parent[3] * world[2]) - (parent[0] * world[1]) + (parent[1] * world[0]) -
                      (parent[2] * world[3]);
  local.rotation[3] = (parent[3] * world[3]) + (parent[0] * world[0]) + (parent[1] * world[1]) +
                      (parent[2] * world[2]);
  const auto length = std::sqrt(std::inner_product(
      std::begin(local.rotation), std::end(local.rotation), std::begin(local.rotation), 0.0F));
  for (auto& component : local.rotation) {
    component /= length;
  }
  output = local;
  return result::success;
}

} // namespace gneiss::editor
