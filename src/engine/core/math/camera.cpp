// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/camera.hpp"
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <numbers>
namespace gneiss::engine {
result build_view_matrix(const transform& value, matrix4& output) noexcept {
  const auto squared = glm::dot(value.rotation, value.rotation);
  if (!std::isfinite(squared) || squared <= 0 || !std::isfinite(value.translation.x) ||
      !std::isfinite(value.translation.y) || !std::isfinite(value.translation.z))
    return result::invalid_argument;
  output = glm::mat4_cast(glm::conjugate(glm::normalize(value.rotation))) *
           glm::translate(matrix4{1.0F}, -value.translation);
  return result::success;
}
result build_perspective_matrix(const camera_settings& camera, float aspect,
                                matrix4& output) noexcept {
  if (!std::isfinite(aspect) || aspect <= 0 || !std::isfinite(camera.fov) ||
      !std::isfinite(camera.near) || !std::isfinite(camera.far) || camera.fov <= 0 ||
      camera.fov >= std::numbers::pi_v<float> || camera.near <= 0 || camera.far <= camera.near)
    return result::invalid_argument;
  output = glm::perspectiveRH_ZO(camera.fov, aspect, camera.near, camera.far);
  return result::success;
}
}
