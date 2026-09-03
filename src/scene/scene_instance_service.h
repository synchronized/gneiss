// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCE_SERVICE_H_
#define GNEISS_SCENE_SCENE_INSTANCE_SERVICE_H_

#include "core/rid_table.h"
#include "render/render_asset_loader.h"
#include "scene/prefab_asset_loader.h"
#include "scene/prefab_runtime_instance.h"
#include "scene/scene_description.h"

#include <gneiss/scene.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::scene_internal {

class scene_instance final {
public:
  scene_instance(gneiss_world world, render_internal::render_asset_loader& loader,
                 prefab_asset_loader& prefab_loader, gneiss_type_registry registry) noexcept;
  ~scene_instance() noexcept;

  scene_instance(const scene_instance&) = delete;
  scene_instance& operator=(const scene_instance&) = delete;

  struct object final {
    std::string uuid;
    std::string name;
    gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
    gneiss_scene_node_id node = GNEISS_NULL_SCENE_NODE_ID;
    render_internal::mesh_asset_lease mesh;
    render_internal::material_asset_lease material;
  };

  void rollback() noexcept;
  [[nodiscard]] gneiss_scene_node_id find_node(std::string_view uuid) const noexcept;
  [[nodiscard]] gneiss_result serialize(std::string& out_json) const;
  [[nodiscard]] gneiss_result get_node_info(std::uint64_t index,
                                            gneiss_scene_instance_node_info& out_info) const;
  [[nodiscard]] std::uint64_t get_prefab_node_count() const noexcept;
  [[nodiscard]] gneiss_result get_prefab_node_info(std::uint64_t index,
                                                   gneiss_scene_prefab_node_info& out_info) const;
  [[nodiscard]] gneiss_result create_prefab_instance(const gneiss_scene_prefab_instance_desc& desc,
                                                     gneiss_scene_node_id* out_root);
  [[nodiscard]] gneiss_result set_prefab_instance_name(gneiss_scene_node_id root,
                                                       std::string_view name);
  [[nodiscard]] gneiss_result set_prefab_source_transform(gneiss_scene_node_id node,
                                                          const gneiss_transform& transform);
  [[nodiscard]] gneiss_result destroy_prefab_instance(gneiss_scene_node_id root) noexcept;
  [[nodiscard]] gneiss_result refresh_prefab_instance(gneiss_scene_node_id root,
                                                      gneiss_scene_node_id* out_new_root,
                                                      gneiss_scene_prefab_refresh_token* out_token);
  [[nodiscard]] gneiss_result toggle_prefab_refresh(gneiss_scene_prefab_refresh_token token,
                                                    gneiss_scene_node_id* out_new_root);
  [[nodiscard]] gneiss_result
  release_prefab_refresh(gneiss_scene_prefab_refresh_token token) noexcept;
  [[nodiscard]] gneiss_result create_node(const gneiss_scene_node_desc& desc,
                                          gneiss_scene_node_id* out_node);
  [[nodiscard]] gneiss_result set_node_name(gneiss_scene_node_id node, std::string_view name);
  [[nodiscard]] gneiss_result reparent_node(gneiss_scene_node_id node, gneiss_scene_node_id parent);
  [[nodiscard]] gneiss_result capture_subtree(gneiss_scene_node_id root,
                                              std::string& out_snapshot) const;
  [[nodiscard]] gneiss_result restore_subtree(std::string_view snapshot,
                                              gneiss_scene_node_id parent,
                                              const gneiss_scene_uuid_mapping* mappings,
                                              std::uint64_t mapping_count,
                                              gneiss_scene_node_id* out_root);
  [[nodiscard]] gneiss_result destroy_subtree(gneiss_scene_node_id root);
  [[nodiscard]] gneiss_result
  create_mesh_renderer_node(const gneiss_scene_mesh_renderer_node_desc& desc,
                            gneiss_scene_node_id* out_node);
  [[nodiscard]] gneiss_result set_mesh_renderer(gneiss_scene_node_id node,
                                                std::string_view mesh_uri,
                                                std::string_view material_uri);
  [[nodiscard]] gneiss_result set_camera(gneiss_scene_node_id node,
                                         const gneiss_scene_camera_desc& desc);
  [[nodiscard]] gneiss_result remove_camera(gneiss_scene_node_id node);
  [[nodiscard]] gneiss_result remove_mesh_renderer(gneiss_scene_node_id node);
  [[nodiscard]] gneiss_result destroy_node(gneiss_scene_node_id node);

  std::vector<object> objects;
  std::vector<std::unique_ptr<prefab_runtime_instance>> prefab_instances;
  scene_description description;

private:
  struct prefab_refresh_transaction final {
    gneiss_scene_prefab_refresh_token token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
    std::string instance_uuid;
    prefab_asset_lease alternate;
  };

  gneiss_world world_;
  render_internal::render_asset_loader& loader_;
  prefab_asset_loader& prefab_loader_;
  gneiss_type_registry registry_;
  std::vector<prefab_refresh_transaction> prefab_refresh_transactions_;
  gneiss_scene_prefab_refresh_token next_prefab_refresh_token_ = 1U;
};

class scene_instance_service final {
public:
  scene_instance_service(gneiss_world world, const asset_internal::virtual_file_system& file_system,
                         render_internal::render_asset_loader& loader,
                         prefab_asset_loader& prefab_loader) noexcept;
  ~scene_instance_service() noexcept;

  [[nodiscard]] bool is_valid() const noexcept {
    return domain_ != 0U && registry_ != GNEISS_NULL_TYPE_REGISTRY;
  }
  [[nodiscard]] gneiss_result load(std::string_view uri,
                                   gneiss_scene_instance* out_instance) noexcept;
  [[nodiscard]] gneiss_result create_empty(std::string_view scene_uuid,
                                           gneiss_scene_instance* out_instance) noexcept;
  [[nodiscard]] gneiss_result unload(gneiss_scene_instance instance) noexcept;
  [[nodiscard]] gneiss_result find_node(gneiss_scene_instance instance, std::string_view uuid,
                                        gneiss_scene_node_id* out_node) const noexcept;
  [[nodiscard]] gneiss_result serialize(gneiss_scene_instance instance,
                                        std::string& out_json) const noexcept;
  [[nodiscard]] gneiss_result get_node_count(gneiss_scene_instance instance,
                                             std::uint64_t* out_count) const noexcept;
  [[nodiscard]] gneiss_result
  get_node_info(gneiss_scene_instance instance, std::uint64_t index,
                gneiss_scene_instance_node_info* out_info) const noexcept;
  [[nodiscard]] gneiss_result get_prefab_node_count(gneiss_scene_instance instance,
                                                    std::uint64_t* out_count) const noexcept;
  [[nodiscard]] gneiss_result
  get_prefab_node_info(gneiss_scene_instance instance, std::uint64_t index,
                       gneiss_scene_prefab_node_info* out_info) const noexcept;
  [[nodiscard]] gneiss_result create_prefab_instance(gneiss_scene_instance instance,
                                                     const gneiss_scene_prefab_instance_desc& desc,
                                                     gneiss_scene_node_id* out_root) noexcept;
  [[nodiscard]] gneiss_result set_prefab_instance_name(gneiss_scene_instance instance,
                                                       gneiss_scene_node_id root,
                                                       std::string_view name) noexcept;
  [[nodiscard]] gneiss_result
  set_prefab_source_transform(gneiss_scene_instance instance, gneiss_scene_node_id node,
                              const gneiss_transform& transform) noexcept;
  [[nodiscard]] gneiss_result destroy_prefab_instance(gneiss_scene_instance instance,
                                                      gneiss_scene_node_id root) noexcept;
  [[nodiscard]] gneiss_result
  refresh_prefab_instance(gneiss_scene_instance instance, gneiss_scene_node_id root,
                          gneiss_scene_node_id* out_new_root,
                          gneiss_scene_prefab_refresh_token* out_token) noexcept;
  [[nodiscard]] gneiss_result toggle_prefab_refresh(gneiss_scene_instance instance,
                                                    gneiss_scene_prefab_refresh_token token,
                                                    gneiss_scene_node_id* out_new_root) noexcept;
  [[nodiscard]] gneiss_result
  release_prefab_refresh(gneiss_scene_instance instance,
                         gneiss_scene_prefab_refresh_token token) noexcept;
  [[nodiscard]] gneiss_result create_node(gneiss_scene_instance instance,
                                          const gneiss_scene_node_desc& desc,
                                          gneiss_scene_node_id* out_node) noexcept;
  [[nodiscard]] gneiss_result set_node_name(gneiss_scene_instance instance,
                                            gneiss_scene_node_id node,
                                            std::string_view name) noexcept;
  [[nodiscard]] gneiss_result reparent_node(gneiss_scene_instance instance,
                                            gneiss_scene_node_id node,
                                            gneiss_scene_node_id parent) noexcept;
  [[nodiscard]] gneiss_result capture_subtree(gneiss_scene_instance instance,
                                              gneiss_scene_node_id root,
                                              std::string& out_snapshot) const noexcept;
  [[nodiscard]] gneiss_result
  restore_subtree(gneiss_scene_instance instance, std::string_view snapshot,
                  gneiss_scene_node_id parent, const gneiss_scene_uuid_mapping* mappings,
                  std::uint64_t mapping_count, gneiss_scene_node_id* out_root) noexcept;
  [[nodiscard]] gneiss_result destroy_subtree(gneiss_scene_instance instance,
                                              gneiss_scene_node_id root) noexcept;
  [[nodiscard]] gneiss_result
  create_mesh_renderer_node(gneiss_scene_instance instance,
                            const gneiss_scene_mesh_renderer_node_desc& desc,
                            gneiss_scene_node_id* out_node) noexcept;
  [[nodiscard]] gneiss_result set_mesh_renderer(gneiss_scene_instance instance,
                                                gneiss_scene_node_id node,
                                                std::string_view mesh_uri,
                                                std::string_view material_uri) noexcept;
  [[nodiscard]] gneiss_result set_camera(gneiss_scene_instance instance, gneiss_scene_node_id node,
                                         const gneiss_scene_camera_desc& desc) noexcept;
  [[nodiscard]] gneiss_result remove_camera(gneiss_scene_instance instance,
                                            gneiss_scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result remove_mesh_renderer(gneiss_scene_instance instance,
                                                   gneiss_scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result destroy_node(gneiss_scene_instance instance,
                                           gneiss_scene_node_id node) noexcept;

private:
  using instance_ptr = std::unique_ptr<scene_instance>;
  gneiss_world world_;
  const asset_internal::virtual_file_system& file_system_;
  render_internal::render_asset_loader& loader_;
  prefab_asset_loader& prefab_loader_;
  gneiss_type_registry registry_ = GNEISS_NULL_TYPE_REGISTRY;
  std::uint16_t domain_;
  core::rid_table<instance_ptr> instances_;
};

} // namespace gneiss::scene_internal

#endif
