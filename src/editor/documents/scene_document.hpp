// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_EDITOR_DOCUMENTS_SCENE_DOCUMENT_HPP_
#define GNEISS_EDITOR_DOCUMENTS_SCENE_DOCUMENT_HPP_

#include "engine/core/entity_id.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/function/world/world.hpp"
#include "engine/resource/types/scene_description.hpp"
namespace gneiss::runtime {
class application;
}

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::editor {

/** 将资产根内的本地路径转换为规范 asset URI；路径越界时拒绝。 */
[[nodiscard]] gneiss::engine::result make_asset_uri(const std::filesystem::path& asset_root,
                                                    const std::filesystem::path& path,
                                                    std::string& output) noexcept;

/** 生成符合场景 Schema 的随机 UUID。 */
[[nodiscard]] gneiss::engine::result make_editor_uuid(std::string& output) noexcept;

struct scene_node_record final {
  scene_node_id node;
  scene_node_id parent;
  entity_id entity;
  std::string uuid;
  std::string display_name;
  std::string mesh_uri;
  std::string material_uri;
  gneiss::engine::transform local_transform = {};
  std::uint32_t component_flags = 0U;
  gneiss::engine::camera_description camera{};
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
  gneiss::engine::transform local_transform = {};
  gneiss::engine::transform source_local_transform = {};
  std::uint32_t override_flags = 0U;
  bool is_instance_root = false;
  bool is_read_only = true;
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
  // 历史记录保活资源，删除后撤销无需重新加载。
  std::vector<engine::scene_mesh_binding> resources;
};

/** 单个场景的编辑文档：保存 URI、选择、脏状态和节点查询快照。
 * 借用 Application，绑定一个运行时场景实例；close 负责卸载该实例。
 * 不拥有 World 或第二棵运行时场景树；须先于 Application 关闭。
 * 撤销命令由宿主持有的 editor_command_history 管理，并在文档切换时清理。
 */
class scene_document final {
public:
  scene_document() noexcept = default;
  ~scene_document() noexcept { close(); }
  scene_document(const scene_document&) = delete;
  scene_document& operator=(const scene_document&) = delete;
  scene_document(scene_document&& other) noexcept;
  scene_document& operator=(scene_document&& other) noexcept;
  /** 接收已经激活的场景；成功后拥有句柄，所属线程调用。 */
  [[nodiscard]] gneiss::engine::result adopt(runtime::application& application,
                                             gneiss::engine::world* world,
                                             gneiss::scene_instance_id scene,
                                             std::string_view uri) noexcept;
  [[nodiscard]] gneiss::engine::result create_empty(runtime::application& application,
                                                    gneiss::engine::world* world,
                                                    std::string_view scene_uuid) noexcept;
  [[nodiscard]] gneiss::engine::result create_empty(runtime::application& application,
                                                    gneiss::engine::world* world) noexcept;
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  /** 预分配镜像后调用就绪创建；成功后发布节点记录，不加载资产。 */
  [[nodiscard]] gneiss::engine::result commit_mesh_node(
      const scene_node_snapshot& snapshot,
      const std::function<gneiss::engine::result(scene_node_id, scene_node_id&)>& create,
      scene_node_id& output) noexcept;
  [[nodiscard]] gneiss::scene_instance_id scene_handle() const noexcept { return scene_; }
  /** 预分配作者值后调用提交；回调失败不改编辑器镜像，不执行资产加载。 */
  [[nodiscard]] gneiss::engine::result
  commit_mesh_renderer(scene_node_id node, std::string_view mesh_uri, std::string_view material_uri,
                       const std::function<gneiss::engine::result()>& commit) noexcept;
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

  [[nodiscard]] gneiss::engine::result select(scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result validate_selection() noexcept;
  [[nodiscard]] gneiss::engine::result create_node(std::string_view name, scene_node_id parent,
                                                   scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result rename_prefab_instance(scene_node_id root,
                                                              std::string_view name) noexcept;
  [[nodiscard]] gneiss::engine::result rename_node(scene_node_id node,
                                                   std::string_view name) noexcept;
  [[nodiscard]] gneiss::engine::result reparent_node(scene_node_id node,
                                                     scene_node_id parent) noexcept;
  [[nodiscard]] gneiss::engine::result
  get_world_transform(scene_node_id node, gneiss::engine::transform& output) const noexcept;
  /** 设置节点局部 TRS；成功后刷新缓存并标记场景已修改。 */
  [[nodiscard]] gneiss::engine::result
  set_local_transform(scene_node_id node, const gneiss::engine::transform& value) noexcept;
  /** 恢复 Prefab 来源节点的单个 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::engine::result
  restore_prefab_transform_field(scene_node_id node, std::uint32_t field_id,
                                 gneiss::engine::transform& out_previous) noexcept;
  /** 恢复 Prefab 来源节点的全部 Transform 字段；返回操作前的有效值。 */
  [[nodiscard]] gneiss::engine::result
  restore_prefab_transform(scene_node_id node, gneiss::engine::transform& out_previous) noexcept;
  [[nodiscard]] gneiss::engine::result
  destroy_subtree(scene_node_id node, scene_subtree_snapshot& out_snapshot) noexcept;
  [[nodiscard]] gneiss::engine::result restore_subtree(const scene_subtree_snapshot& snapshot,
                                                       scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result duplicate_subtree(scene_node_id node, scene_node_id parent,
                                                         scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_camera(scene_node_id node, const gneiss::engine::camera_description& desc) noexcept;
  [[nodiscard]] gneiss::engine::result remove_camera(scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result remove_mesh_renderer(scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result create_mesh_renderer_node(std::string_view name,
                                                                 std::string_view mesh_uri,
                                                                 std::string_view material_uri,
                                                                 scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result set_mesh_renderer(scene_node_id node,
                                                         std::string_view mesh_uri,
                                                         std::string_view material_uri) noexcept;
  [[nodiscard]] gneiss::engine::result
  restore_mesh_renderer_node(const scene_node_snapshot& snapshot, scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_node(scene_node_id node,
                                                    scene_node_snapshot& out_snapshot) noexcept;
  /** 将当前场景原子替换到原 asset URI；成功后清除脏状态。 */
  [[nodiscard]] gneiss::engine::result save(const std::filesystem::path& asset_root,
                                            std::string* saved_content = nullptr) noexcept;
  /** 将当前场景写入尚不存在的 asset URI，并将其设为后续保存目标。 */
  [[nodiscard]] gneiss::engine::result save_as(const std::filesystem::path& asset_root,
                                               std::string_view uri,
                                               std::string* saved_content = nullptr) noexcept;
  /** 场景直接修改后重建视图；失败清空缓存，避免继续暴露失效节点。 */
  [[nodiscard]] gneiss::engine::result synchronize_nodes() noexcept;
  void mark_dirty() noexcept { is_dirty_ = true; }
  void clear_dirty() noexcept { is_dirty_ = false; }

private:
  engine::result capture_subtree_bindings(scene_node_id root,
                                          std::vector<engine::scene_mesh_binding>& output) const;
  template <class F> gneiss::engine::result with_scene(F&& action) const noexcept;
  template <class F>
  gneiss::engine::result with_node(scene_node_id& output, F&& action) const noexcept;
  [[nodiscard]] gneiss::engine::result refresh_nodes() noexcept;
  [[nodiscard]] gneiss::engine::result save_to(const std::filesystem::path& asset_root,
                                               std::string_view uri, bool require_existing,
                                               std::string* saved_content) noexcept;
  [[nodiscard]] gneiss::engine::result
  create_mesh_renderer_node(const scene_node_snapshot& snapshot, scene_node_id& out_node) noexcept;
  gneiss::engine::world* world_ = nullptr;
  // 借用宿主；宿主必须在关闭/销毁 Application 前关闭编辑文档。
  runtime::application* application_{};
  gneiss::scene_instance_id scene_{};
  std::vector<scene_node_record> nodes_;
  std::vector<prefab_node_record> prefab_nodes_;
  scene_node_id selection_;
  std::string uri_;
  bool is_dirty_ = false;
};

} // namespace gneiss::editor

#endif
