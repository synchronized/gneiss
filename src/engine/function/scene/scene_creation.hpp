// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/transform.hpp"

#include "engine/function/scene/scene_types.hpp"

#include <string_view>

namespace gneiss::engine {

/** 创建调用期间借用字符串；成功后场景独立拥有作者数据。 */
struct node_creation {
  guid uuid;
  std::string_view name;
  scene_node_id parent{};
  math::transform local_transform = {};
};

struct mesh_renderer_node_creation {
  guid uuid;
  std::string_view name;
  scene_node_id parent{};
  std::string_view mesh_uri;
  std::string_view material_uri;
};

struct prefab_creation {
  guid instance_uuid;
  std::string_view name;
  std::string_view prefab_uri;
  scene_node_id parent{};
  math::transform local_transform = {};
};

/** 子树恢复期间借用；UUID 重写规则与唯一性仍由 Scene 校验。 */
struct uuid_mapping {
  guid source_uuid;
  guid target_uuid;
};

} // namespace gneiss::engine
