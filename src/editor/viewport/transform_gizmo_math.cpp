// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_math.hpp"
#include "engine/core/math/transform.hpp"
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <numbers>
namespace gneiss::editor {
namespace {
using namespace gneiss::engine;
bool positive_scale(const transform& value) noexcept {
  return std::ranges::all_of(components(value.scale), [](float v) { return v > 0; });
}
}
gizmo_matrix build_gizmo_projection_matrix(float aspect) noexcept {
  if (!std::isfinite(aspect) || aspect <= 0)
    return {};
  return engine::matrix_to_array(
      glm::perspectiveRH_ZO(std::numbers::pi_v<float> / 3, aspect, 0.05F, 2000.0F));
}

gizmo_matrix build_gizmo_view_matrix(const engine::transform& camera) noexcept {
  const auto squared = glm::dot(camera.rotation, camera.rotation);
  if (!engine::is_finite(camera.translation) || !std::isfinite(squared) || squared <= 0)
    return {};
  return engine::matrix_to_array(glm::mat4_cast(glm::conjugate(glm::normalize(camera.rotation))) *
                                 glm::translate(engine::matrix4{1.0F}, -camera.translation));
}
engine::result transform_to_gizmo_matrix(const engine::transform& value,
                                         gizmo_matrix& output) noexcept {
  if (!engine::valid_transform(value) || !positive_scale(value))
    return engine::result::unsupported;
  output = engine::matrix_to_array(engine::transform_matrix(value));
  return engine::result::success;
}
engine::result gizmo_matrix_to_transform(const gizmo_matrix& value,
                                         engine::transform& output) noexcept {
  if (!std::ranges::all_of(value, [](float v) { return std::isfinite(v); }) ||
      std::abs(value[15] - 1.0F) > 1.0e-4F || value[3] != 0 || value[7] != 0 || value[11] != 0)
    return engine::result::invalid_argument;
  const auto matrix = engine::matrix_from_array(value);
  engine::transform pending;
  pending.translation = engine::vector3{matrix[3]};
  engine::matrix3 rotation{matrix};
  for (glm::length_t column = 0; column < 3; ++column) {
    pending.scale[column] = glm::length(rotation[column]);
    if (pending.scale[column] < 1.0e-6F)
      return engine::result::unsupported;
    rotation[column] /= pending.scale[column];
  }
  if (glm::determinant(rotation) <= 0 ||
      std::max({std::abs(glm::dot(rotation[0], rotation[1])),
                std::abs(glm::dot(rotation[0], rotation[2])),
                std::abs(glm::dot(rotation[1], rotation[2]))}) > 1.0e-3F)
    return engine::result::unsupported;
  pending.rotation = glm::normalize(glm::quat_cast(rotation));
  output = pending;
  return engine::result::success;
}
engine::result world_to_local_transform(const engine::transform* parent,
                                        const engine::transform& world,
                                        engine::transform& output) noexcept {
  if (!engine::valid_transform(world) || (parent && !engine::valid_transform(*parent)))
    return engine::result::invalid_argument;
  if (!positive_scale(world) || (parent && !positive_scale(*parent)))
    return engine::result::unsupported;
  if (!parent) {
    output = world;
    return engine::result::success;
  }
  const auto inverse = glm::conjugate(parent->rotation);
  output = {(inverse * (world.translation - parent->translation)) / parent->scale,
            glm::normalize(inverse * world.rotation), world.scale / parent->scale};
  return engine::result::success;
}
}
