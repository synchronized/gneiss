// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace gneiss::editor {
namespace {

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
