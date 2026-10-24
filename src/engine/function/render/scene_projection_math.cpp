// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/scene_projection_math.hpp"
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
namespace gneiss::engine {
bool build_model_matrices(const transform& value, matrix4& model, matrix4& normal) noexcept {
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
}
