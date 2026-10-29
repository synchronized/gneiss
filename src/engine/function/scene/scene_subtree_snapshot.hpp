// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include <map>
namespace gneiss::engine {
/** 每个显式对象实际使用的资源版本，按对象 UUID 而非 URI 关联。 */
struct scene_object_resources {
  scene_mesh_binding renderer;
  prefab_asset_ref prefab;
  std::vector<scene_mesh_binding> source_nodes;
};
/** 普通对象与 Prefab 共用的内存子树快照；不持有运行期节点。 */
struct scene_subtree_snapshot {
  scene_description description;
  std::string root_uuid;
  std::string parent_uuid;
  std::map<std::string, scene_object_resources, std::less<>> resources;
};
}
