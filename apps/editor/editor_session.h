// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_SESSION_H_
#define GNEISS_APPS_EDITOR_EDITOR_SESSION_H_

#include <gneiss/scene.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

/** 将资产根内的本地路径转换为规范 asset URI；路径越界时拒绝。 */
[[nodiscard]] result make_asset_uri(const std::filesystem::path& asset_root,
                                    const std::filesystem::path& path,
                                    std::string& output) noexcept;

struct scene_node_record final {
  scene_node_id node;
  scene_node_id parent;
  entity_id entity;
  std::string uuid;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
  transform local_transform = GNEISS_TRANSFORM_IDENTITY;
  std::uint32_t component_flags = 0U;
  gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  bool is_primary_camera = false;
};

struct prefab_node_record final {
  scene_node_id node;
  scene_node_id parent;
  entity_id entity;
  std::string instance_uuid;
  std::string source_node_uuid;
  std::string display_name;
  std::string prefab_uri;
  transform local_transform = GNEISS_TRANSFORM_IDENTITY;
  transform source_local_transform = GNEISS_TRANSFORM_IDENTITY;
  std::uint32_t override_flags = 0U;
  bool is_instance_root = false;
  bool is_read_only = true;
};

struct prefab_instance_snapshot final {
  std::string instance_uuid;
  std::string parent_uuid;
  std::string display_name;
  std::string prefab_uri;
  transform local_transform = GNEISS_TRANSFORM_IDENTITY;
};

struct scene_node_snapshot final {
  std::string uuid;
  std::string parent_uuid;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
};

struct scene_subtree_snapshot final {
  std::string json;
  std::string parent_uuid;
  std::string root_uuid;
};

class editor_session final {
public:
  [[nodiscard]] result open(gneiss_application application, gneiss_world world,
                            std::string_view uri) noexcept;
  [[nodiscard]] result create_empty(gneiss_application application, gneiss_world world,
                                    std::string_view scene_uuid) noexcept;
  [[nodiscard]] result create_empty(gneiss_application application, gneiss_world world) noexcept;
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return scene_.is_valid(); }
  [[nodiscard]] bool is_dirty() const noexcept { return is_dirty_; }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] const std::vector<scene_node_record>& nodes() const noexcept { return nodes_; }
  [[nodiscard]] const std::vector<prefab_node_record>& prefab_nodes() const noexcept {
    return prefab_nodes_;
  }
  [[nodiscard]] scene_node_id selection() const noexcept { return selection_; }
  [[nodiscard]] const scene_node_record* selected_node() const noexcept;
  [[nodiscard]] const prefab_node_record* selected_prefab_node() const noexcept;
  [[nodiscard]] const prefab_node_record*
  find_prefab_root(std::string_view instance_uuid) const noexcept;
  [[nodiscard]] const prefab_node_record*
  find_prefab_source(std::string_view instance_uuid, std::string_view source_uuid) const noexcept;
  [[nodiscard]] const scene_node_record* find_node(std::string_view uuid) const noexcept;

  [[nodiscard]] result select(scene_node_id node) noexcept;
  [[nodiscard]] result validate_selection() noexcept;
  [[nodiscard]] result create_node(std::string_view name, scene_node_id parent,
                                   scene_node_id& out_node) noexcept;
  [[nodiscard]] result create_prefab_instance(std::string_view name, std::string_view prefab_uri,
                                              scene_node_id parent,
                                              scene_node_id& out_root) noexcept;
  [[nodiscard]] result rename_prefab_instance(scene_node_id root, std::string_view name) noexcept;
  [[nodiscard]] result destroy_prefab_instance(scene_node_id root,
                                               prefab_instance_snapshot& out_snapshot) noexcept;
  [[nodiscard]] result restore_prefab_instance(const prefab_instance_snapshot& snapshot,
                                               scene_node_id& out_root) noexcept;
  [[nodiscard]] result
  refresh_prefab_instance(scene_node_id root, scene_node_id& out_new_root,
                          gneiss_scene_prefab_refresh_token& out_token) noexcept;
  [[nodiscard]] result toggle_prefab_refresh(gneiss_scene_prefab_refresh_token token,
                                             scene_node_id& out_new_root) noexcept;
  void release_prefab_refresh(gneiss_scene_prefab_refresh_token token) noexcept;
  [[nodiscard]] result rename_node(scene_node_id node, std::string_view name) noexcept;
  [[nodiscard]] result reparent_node(scene_node_id node, scene_node_id parent) noexcept;
  /** 设置节点局部 TRS；成功后刷新缓存并标记场景已修改。 */
  [[nodiscard]] result set_local_transform(scene_node_id node, const transform& value) noexcept;
  [[nodiscard]] result destroy_subtree(scene_node_id node,
                                       scene_subtree_snapshot& out_snapshot) noexcept;
  [[nodiscard]] result restore_subtree(const scene_subtree_snapshot& snapshot,
                                       scene_node_id& out_node) noexcept;
  [[nodiscard]] result duplicate_subtree(scene_node_id node, scene_node_id parent,
                                         scene_node_id& out_node) noexcept;
  [[nodiscard]] result set_camera(scene_node_id node, const scene_camera_desc& desc) noexcept;
  [[nodiscard]] result remove_camera(scene_node_id node) noexcept;
  [[nodiscard]] result remove_mesh_renderer(scene_node_id node) noexcept;
  [[nodiscard]] result create_mesh_renderer_node(std::string_view name, std::string_view mesh_uri,
                                                 std::string_view material_uri,
                                                 scene_node_id& out_node) noexcept;
  [[nodiscard]] result set_mesh_renderer(scene_node_id node, std::string_view mesh_uri,
                                         std::string_view material_uri) noexcept;
  [[nodiscard]] result restore_mesh_renderer_node(const scene_node_snapshot& snapshot,
                                                  scene_node_id& out_node) noexcept;
  [[nodiscard]] result destroy_node(scene_node_id node, scene_node_snapshot& out_snapshot) noexcept;
  /** 将当前场景原子替换到原 asset URI；成功后清除脏状态。 */
  [[nodiscard]] result save(const std::filesystem::path& asset_root) noexcept;
  /** 将当前场景写入尚不存在的 asset URI，并将其设为后续保存目标。 */
  [[nodiscard]] result save_as(const std::filesystem::path& asset_root,
                               std::string_view uri) noexcept;
  void mark_dirty() noexcept { is_dirty_ = true; }
  void clear_dirty() noexcept { is_dirty_ = false; }

private:
  [[nodiscard]] result refresh_nodes() noexcept;
  [[nodiscard]] result save_to(const std::filesystem::path& asset_root, std::string_view uri,
                               bool require_existing) noexcept;
  [[nodiscard]] result create_mesh_renderer_node(const scene_node_snapshot& snapshot,
                                                 scene_node_id& out_node) noexcept;
  gneiss_world world_ = GNEISS_NULL_WORLD;
  scene_instance scene_;
  std::vector<scene_node_record> nodes_;
  std::vector<prefab_node_record> prefab_nodes_;
  scene_node_id selection_;
  std::string uri_;
  bool is_dirty_ = false;
};

} // namespace gneiss::editor

#endif
