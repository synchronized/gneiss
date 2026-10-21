// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/math/transform.hpp"

#include "engine/resource/types/scene_description.hpp"

#include <gneiss/engine/core/entity.hpp>
#include <gneiss/engine/core/scene_types.hpp>

#include <optional>
#include <string_view>

namespace gneiss::scene_internal {

enum class node_query_detail { identity, assets, components };

/** 字符串借用到下次场景修改或销毁；查询不延长场景寿命。 */
struct scene_node_view {
  scene_node_id node{};
  scene_node_id parent{};
  entity_id entity{};
  std::string_view uuid;
  std::string_view name;
  std::string_view mesh_uri;
  std::string_view material_uri;
  gneiss::engine::transform local_transform = {};
  scene_node_components component_flags{};
  std::optional<gneiss::engine::resource::camera_description> camera;
};

struct prefab_node_view {
  scene_prefab_flags flags{};
  scene_node_id node{};
  scene_node_id parent{};
  entity_id entity{};
  std::string_view instance_uuid;
  std::string_view source_node_uuid;
  std::string_view name;
  std::string_view prefab_uri;
  gneiss::engine::transform local_transform = {};
  gneiss::engine::transform source_local_transform = {};
};

} // namespace gneiss::scene_internal
