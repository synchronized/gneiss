// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/transform.hpp"

namespace gneiss::engine {
vector3 transform_normal(const vector3& normal,
                         const gneiss::engine::transform& transform) noexcept {
  const auto value =
      glm::normalize(transform.rotation *
                     (gneiss::engine::vector3{normal[0], normal[1], normal[2]} / transform.scale));
  return {value.x, value.y, value.z};
}

bool model_and_normal_matrices(const transform& value, matrix4& model, matrix4& normal) noexcept {
  const auto squared = glm::dot(value.rotation, value.rotation);
  if (!std::isfinite(squared) || squared <= 0)
    return false;
  for (const auto v : components(value.translation))
    if (!std::isfinite(v))
      return false;
  for (const auto v : components(value.scale))
    if (!std::isfinite(v) || v == 0)
      return false;
  const auto rotation = glm::mat4_cast(glm::normalize(value.rotation));
  model = glm::translate(matrix4{1.0F}, value.translation) * glm::scale(rotation, value.scale);
  normal = glm::scale(rotation, vector3{1.0F} / value.scale);
  return true;
}
} // namespace gneiss::engine
