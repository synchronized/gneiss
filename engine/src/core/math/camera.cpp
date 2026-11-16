// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/math/camera.hpp>

#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <numbers>

namespace gneiss::math {

bool camera_settings::valid() const noexcept {
  return std::isfinite(fov) && std::isfinite(near_plane) && std::isfinite(far_plane) && fov > 0 &&
         fov < std::numbers::pi_v<float> && near_plane > 0 && far_plane > near_plane;
}

error view_matrix_rh(const transform& value, matrix4& output) noexcept {
  const auto squared = glm::dot(value.rotation, value.rotation);
  if (!std::isfinite(squared) || squared <= 0 || !std::isfinite(value.translation.x) ||
      !std::isfinite(value.translation.y) || !std::isfinite(value.translation.z))
    return error::invalid_argument;
  output = glm::mat4_cast(glm::conjugate(glm::normalize(value.rotation))) *
           glm::translate(matrix4{1.0F}, -value.translation);
  return error::success;
}
error perspective_rh_zo(const camera_settings& camera, float aspect, matrix4& output) noexcept {
  if (!std::isfinite(aspect) || aspect <= 0 || !camera.valid())
    return error::invalid_argument;
  output = glm::perspectiveRH_ZO(camera.fov, aspect, camera.near_plane, camera.far_plane);
  return error::success;
}
}
