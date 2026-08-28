// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_SESSION_H_
#define GNEISS_APPS_EDITOR_EDITOR_SESSION_H_

#include <gneiss/scene.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

struct scene_node_record final {
  scene_node_id node;
  scene_node_id parent;
  entity_id entity;
  std::string uuid;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
};

struct scene_node_snapshot final {
  std::string uuid;
  std::string parent_uuid;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
};

class editor_session final {
public:
  [[nodiscard]] result open(gneiss_application application, gneiss_world world,
                            std::string_view uri) noexcept;
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return scene_.is_valid(); }
  [[nodiscard]] bool is_dirty() const noexcept { return is_dirty_; }
  [[nodiscard]] std::string_view uri() const noexcept { return uri_; }
  [[nodiscard]] const std::vector<scene_node_record>& nodes() const noexcept { return nodes_; }
  [[nodiscard]] scene_node_id selection() const noexcept { return selection_; }
  [[nodiscard]] const scene_node_record* selected_node() const noexcept;
  [[nodiscard]] const scene_node_record* find_node(std::string_view uuid) const noexcept;

  [[nodiscard]] result select(scene_node_id node) noexcept;
  [[nodiscard]] result validate_selection() noexcept;
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
  void mark_dirty() noexcept { is_dirty_ = true; }
  void clear_dirty() noexcept { is_dirty_ = false; }

private:
  [[nodiscard]] result create_mesh_renderer_node(const scene_node_snapshot& snapshot,
                                                 scene_node_id& out_node) noexcept;
  gneiss_world world_ = GNEISS_NULL_WORLD;
  scene_instance scene_;
  std::vector<scene_node_record> nodes_;
  scene_node_id selection_;
  std::string uri_;
  bool is_dirty_ = false;
};

} // namespace gneiss::editor

#endif
