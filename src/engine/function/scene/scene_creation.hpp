// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/scene.h>

#include <string_view>

namespace gneiss::scene_internal {

/** 创建调用期间借用字符串；成功后场景独立拥有作者数据。 */
struct node_creation {
  std::string_view uuid;
  std::string_view name;
  gneiss_scene_node_id parent{};
  gneiss_transform local_transform = GNEISS_TRANSFORM_IDENTITY;
};

struct mesh_renderer_node_creation {
  std::string_view uuid;
  std::string_view name;
  gneiss_scene_node_id parent{};
  std::string_view mesh_uri;
  std::string_view material_uri;
};

struct prefab_creation {
  std::string_view instance_uuid;
  std::string_view name;
  std::string_view prefab_uri;
  gneiss_scene_node_id parent{};
  gneiss_transform local_transform = GNEISS_TRANSFORM_IDENTITY;
};

/** 子树恢复期间借用；UUID 重写规则与唯一性仍由 Scene 校验。 */
struct uuid_mapping {
  std::string_view source_uuid;
  std::string_view target_uuid;
};

} // namespace gneiss::scene_internal
