// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_math.hpp"
#include <gneiss/engine/core/math/camera.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace gneiss::editor {
namespace {
using namespace gneiss;
bool positive_scale(const math::transform& value) noexcept {
  return std::ranges::all_of(math::components(value.scale), [](float v) { return v > 0; });
}
}
gizmo_matrix build_gizmo_projection_matrix(float aspect) noexcept {
  gneiss::math::matrix4 output;
  if (gneiss::math::perspective_rh_zo({std::numbers::pi_v<float> / 3, 0.05F, 2000.0F}, aspect,
                                      output)
          .failed())
    return {};
  return gneiss::math::matrix_to_array(output);
}

gizmo_matrix build_gizmo_view_matrix(const gneiss::math::transform& camera) noexcept {
  gneiss::math::matrix4 output;
  if (gneiss::math::view_matrix_rh(camera, output).failed())
    return {};
  return gneiss::math::matrix_to_array(output);
}
gneiss::error transform_to_gizmo_matrix(const gneiss::math::transform& value,
                                        gizmo_matrix& output) noexcept {
  if (!gneiss::math::is_valid_transform(value) || !positive_scale(value))
    return gneiss::error::unsupported;
  output = gneiss::math::matrix_to_array(gneiss::math::model_matrix(value));
  return gneiss::error::success;
}
gneiss::error gizmo_matrix_to_transform(const gizmo_matrix& value,
                                        gneiss::math::transform& output) noexcept {
  if (!std::ranges::all_of(value, [](float v) { return std::isfinite(v); }) ||
      std::abs(value[15] - 1.0F) > 1.0e-4F || value[3] != 0 || value[7] != 0 || value[11] != 0)
    return gneiss::error::invalid_argument;
  const auto matrix = gneiss::math::matrix_from_array(value);
  gneiss::math::transform pending;
  pending.translation = gneiss::math::vector3{matrix[3]};
  gneiss::math::matrix3 rotation{matrix};
  for (glm::length_t column = 0; column < 3; ++column) {
    pending.scale[column] = glm::length(rotation[column]);
    if (pending.scale[column] < 1.0e-6F)
      return gneiss::error::unsupported;
    rotation[column] /= pending.scale[column];
  }
  if (glm::determinant(rotation) <= 0 ||
      std::max({std::abs(glm::dot(rotation[0], rotation[1])),
                std::abs(glm::dot(rotation[0], rotation[2])),
                std::abs(glm::dot(rotation[1], rotation[2]))}) > 1.0e-3F)
    return gneiss::error::unsupported;
  pending.rotation = glm::normalize(glm::quat_cast(rotation));
  output = pending;
  return gneiss::error::success;
}
gneiss::error world_to_local_transform(const gneiss::math::transform* parent,
                                       const gneiss::math::transform& world,
                                       gneiss::math::transform& output) noexcept {
  if (!gneiss::math::is_valid_transform(world) ||
      (parent && !gneiss::math::is_valid_transform(*parent)))
    return gneiss::error::invalid_argument;
  if (!positive_scale(world) || (parent && !positive_scale(*parent)))
    return gneiss::error::unsupported;
  if (!parent) {
    output = world;
    return gneiss::error::success;
  }
  const auto inverse = glm::conjugate(parent->rotation);
  output = {(inverse * (world.translation - parent->translation)) / parent->scale,
            glm::normalize(inverse * world.rotation), world.scale / parent->scale};
  return gneiss::error::success;
}
}
