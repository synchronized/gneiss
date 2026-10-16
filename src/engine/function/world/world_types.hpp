// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/math/transform.hpp"
#include "engine/function/world/world_components.hpp"
#include <gneiss/engine/scene.h>

namespace gneiss::world_internal {
using camera_settings = render_internal::camera_settings;
struct mesh_renderer_replacement {
  gneiss_entity_id entity{};
  mesh_renderer expected{};
  mesh_renderer replacement{};
};
/** 不持有资源；调用方保留 renderer 对应租约，候选相机不自动成为活动相机。 */
struct staged_object_description {
  gneiss_scene_node_id parent{};
  engine::transform transform;
  const camera_settings* camera{};
  const mesh_renderer* renderer{};
};
struct staged_object {
  gneiss_entity_id entity{};
  gneiss_scene_node_id node{};
};
}
