// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/math/transform.hpp"
#include "engine/function/world/world_components.hpp"
#include <gneiss/engine/core/entity.hpp>
#include <gneiss/engine/core/scene_types.hpp>

namespace gneiss::engine {
struct mesh_renderer_replacement {
  entity_id entity{};
  mesh_renderer expected{};
  mesh_renderer replacement{};
};
/** 不持有资源；调用方保留 renderer 对应租约，候选相机不自动成为活动相机。 */
struct staged_object_description {
  scene_node_id parent{};
  gneiss::engine::transform transform;
  const camera_settings* camera{};
  const mesh_renderer* renderer{};
};
struct staged_object {
  entity_id entity{};
  scene_node_id node{};
};
}
