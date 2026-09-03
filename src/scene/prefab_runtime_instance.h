// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_PREFAB_RUNTIME_INSTANCE_H_
#define GNEISS_SCENE_PREFAB_RUNTIME_INSTANCE_H_

#include "render/render_asset_loader.h"
#include "scene/prefab_asset_loader.h"
#include "scene/prefab_description.h"

#include <gneiss/scene.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::scene_internal {

/** 一份已提交到 World 的 Prefab Runtime 投影；析构时释放全部节点、实体和资产租约。 */
class prefab_runtime_instance final {
public:
  struct node_info final {
    const prefab_author_address* address = nullptr;
    std::string_view name;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
    gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  };

  prefab_runtime_instance(gneiss_world world, render_internal::render_asset_loader& loader,
                          prefab_asset_lease prefab, std::string instance_uuid) noexcept;
  ~prefab_runtime_instance() noexcept;

  prefab_runtime_instance(const prefab_runtime_instance&) = delete;
  prefab_runtime_instance& operator=(const prefab_runtime_instance&) = delete;

  [[nodiscard]] static gneiss_result
  create(gneiss_world world, render_internal::render_asset_loader& loader,
         prefab_asset_lease prefab, std::string_view instance_uuid, gneiss_scene_node_id parent,
         const gneiss_transform& root_transform,
         std::unique_ptr<prefab_runtime_instance>& out_instance) noexcept;

  [[nodiscard]] gneiss_scene_node_id root() const noexcept { return root_node_; }
  [[nodiscard]] gneiss_entity_id root_entity() const noexcept { return root_entity_; }
  [[nodiscard]] prefab_asset_lease prefab_lease() const noexcept { return prefab_; }
  [[nodiscard]] gneiss_scene_node_id find_node(std::string_view source_node_uuid) const noexcept;
  [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
  [[nodiscard]] gneiss_result get_node_info(std::size_t index, node_info& out_info) const noexcept;

private:
  struct runtime_node final {
    prefab_author_address address;
    std::string name;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
    render_internal::mesh_asset_lease mesh;
    render_internal::material_asset_lease material;
  };

  void rollback() noexcept;
  [[nodiscard]] gneiss_result stage_assets(const prefab_description& description);
  [[nodiscard]] gneiss_result commit(const prefab_description& description,
                                     gneiss_scene_node_id parent,
                                     const gneiss_transform& root_transform);

  gneiss_world world_;
  render_internal::render_asset_loader& loader_;
  prefab_asset_lease prefab_;
  std::string instance_uuid_;
  gneiss_entity_id root_entity_ = GNEISS_NULL_ENTITY_ID;
  gneiss_scene_node_id root_node_ = GNEISS_NULL_SCENE_NODE_ID;
  std::vector<runtime_node> nodes_;
};

} // namespace gneiss::scene_internal

#endif
