// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/scene/scene_description.hpp"

#include <gneiss/scene.h>

#include <optional>
#include <string_view>

namespace gneiss::scene_internal {

enum class node_query_detail { identity, assets, components };

/** 字符串借用到下次场景修改或销毁；查询不延长场景寿命。 */
struct scene_node_view {
  gneiss_scene_node_id node{};
  gneiss_scene_node_id parent{};
  gneiss_entity_id entity{};
  std::string_view uuid;
  std::string_view name;
  std::string_view mesh_uri;
  std::string_view material_uri;
  gneiss_transform local_transform = GNEISS_TRANSFORM_IDENTITY;
  std::uint32_t component_flags{};
  std::optional<camera_description> camera;
};

struct prefab_node_view {
  std::uint32_t flags{};
  gneiss_scene_node_id node{};
  gneiss_scene_node_id parent{};
  gneiss_entity_id entity{};
  std::string_view instance_uuid;
  std::string_view source_node_uuid;
  std::string_view name;
  std::string_view prefab_uri;
  gneiss_transform local_transform = GNEISS_TRANSFORM_IDENTITY;
  gneiss_transform source_local_transform = GNEISS_TRANSFORM_IDENTITY;
};

} // namespace gneiss::scene_internal
