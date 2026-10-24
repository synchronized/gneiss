// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include "engine/core/entity_id.hpp"
#include "engine/core/math/camera_settings.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/values/core/result.hpp"
#include <gneiss/engine/scene.h>

// 仅保留尚未撤销的 C 边界值转换；场景描述与查询使用原生接口。
namespace gneiss {
[[nodiscard]] constexpr gneiss_camera_desc
to_native(const engine::camera_settings& value) noexcept {
  return {
      .struct_size = sizeof(gneiss_camera_desc),
      .reserved = 0,
      .vertical_field_of_view_radians = value.vertical_field_of_view_radians,
      .near_plane = value.near_plane,
      .far_plane = value.far_plane,
  };
}

[[nodiscard]] constexpr engine::camera_settings
from_native(const gneiss_camera_desc& value) noexcept {
  return {
      .vertical_field_of_view_radians = value.vertical_field_of_view_radians,
      .near_plane = value.near_plane,
      .far_plane = value.far_plane,
  };
}

[[nodiscard]] constexpr gneiss_transform to_native(const engine::transform& value) noexcept {
  return {
      .translation = {value.translation[0], value.translation[1], value.translation[2]},
      .rotation = {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]},
      .scale = {value.scale[0], value.scale[1], value.scale[2]},
  };
}

[[nodiscard]] constexpr engine::transform from_native(const gneiss_transform& value) noexcept {
  return {
      .translation = {value.translation[0], value.translation[1], value.translation[2]},
      .rotation = {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]},
      .scale = {value.scale[0], value.scale[1], value.scale[2]},
  };
}

} // namespace gneiss
