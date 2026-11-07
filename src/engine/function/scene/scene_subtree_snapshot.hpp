// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/guid.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/resource/prefab.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include "engine/resource/scene/scene_record.hpp"
#include <map>
namespace gneiss::engine {
/** 每个显式对象实际使用的资源版本，按对象 UUID 而非 URI 关联。 */
struct scene_object_resources {
  scene_mesh_binding renderer;
  prefab_record prefab_content;
  [[nodiscard]] GNEISS_API result<prefab_asset_ref> prefab_source() const noexcept;
  std::vector<scene_mesh_binding> source_nodes;
  ref<const engine::prefab> source_asset{};
};
/** 普通对象与 Prefab 共用的内存子树快照；不持有运行期节点。 */
struct scene_subtree_snapshot {
  scene_record content;
  [[nodiscard]] GNEISS_API result<scene_description> description() const noexcept;
  guid root_uuid;
  guid parent_uuid;
  std::map<guid, scene_object_resources> resources;
};
}
