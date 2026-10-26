// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/meta/type_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include <map>

namespace gneiss::engine {
class scene_instance final {
public:
  scene_instance(gneiss::engine::world& native_world, gneiss::engine::render_asset_loader& loader,
                 const gneiss::engine::meta::registry_ref& registry) noexcept;
  ~scene_instance() noexcept;

  scene_instance(const scene_instance&) = delete;
  scene_instance& operator=(const scene_instance&) = delete;

  struct object final {
    std::string uuid;
    std::uint64_t entity = UINT64_C(0);
    scene_node_id node = null_scene_node_id;
    gneiss::engine::mesh mesh;
    gneiss::engine::material material;
  };

  void rollback() noexcept;
  /** 仅用于不可见候选 World，分阶段入口不执行文件读取。 */
  void initialize_staged(gneiss::engine::scene_description candidate);
  [[nodiscard]] gneiss::engine::result create_staged_node(std::size_t index, scene_node_id parent,
                                                          gneiss::engine::mesh mesh,
                                                          gneiss::engine::material material);

  [[nodiscard]] scene_node_id find_node(std::string_view uuid) const noexcept;
  [[nodiscard]] gneiss::engine::result capture_description(scene_description& output) const;
  [[nodiscard]] gneiss::engine::result serialize(std::string& out_json) const;
  [[nodiscard]] gneiss::engine::result get_node_info(std::uint64_t index, scene_node_view& out_info,
                                                     node_query_detail detail) const;
  [[nodiscard]] std::uint64_t get_prefab_node_count() const noexcept;
  [[nodiscard]] gneiss::engine::result get_prefab_node_info(std::uint64_t index,
                                                            prefab_node_view& out_info) const;
  [[nodiscard]] gneiss::engine::result create_prefab_instance(const prefab_creation& desc,
                                                              scene_node_id* out_root);
  [[nodiscard]] gneiss::engine::result set_prefab_instance_name(scene_node_id root,
                                                                std::string_view name);
  [[nodiscard]] gneiss::engine::result
  set_prefab_source_transform(scene_node_id node, const gneiss::engine::transform& transform);
  [[nodiscard]] gneiss::engine::result destroy_prefab_instance(scene_node_id root) noexcept;
  [[nodiscard]] gneiss::engine::result refresh_prefab_instance(scene_node_id root,
                                                               scene_node_id* out_new_root,
                                                               prefab_refresh_token* out_token);
  [[nodiscard]] gneiss::engine::result toggle_prefab_refresh(prefab_refresh_token token,
                                                             scene_node_id* out_new_root);
  [[nodiscard]] gneiss::engine::result release_prefab_refresh(prefab_refresh_token token) noexcept;
  [[nodiscard]] gneiss::engine::result create_node(const node_creation& desc,
                                                   scene_node_id* out_node);
  [[nodiscard]] gneiss::engine::result set_node_name(scene_node_id node, std::string_view name);
  [[nodiscard]] gneiss::engine::result reparent_node(scene_node_id node, scene_node_id parent);
  [[nodiscard]] gneiss::engine::result capture_subtree(scene_node_id root,
                                                       std::string& out_snapshot) const;
  [[nodiscard]] gneiss::engine::result
  restore_subtree(std::string_view snapshot, scene_node_id parent,
                  std::span<const uuid_mapping> mappings, scene_node_id* out_root,
                  std::span<const scene_mesh_binding> bindings);
  [[nodiscard]] gneiss::engine::result destroy_subtree(scene_node_id root);
  [[nodiscard]] gneiss::engine::result
  create_mesh_renderer_node(const mesh_renderer_node_creation& desc, scene_node_id* out_node,
                            const scene_mesh_binding* prepared = nullptr);
  [[nodiscard]] gneiss::engine::result
  set_mesh_renderer(scene_node_id node, std::string_view mesh_uri, std::string_view material_uri);
  [[nodiscard]] gneiss::engine::result set_camera(scene_node_id node,
                                                  const gneiss::engine::camera_description& desc);
  [[nodiscard]] gneiss::engine::result remove_camera(scene_node_id node);
  [[nodiscard]] gneiss::engine::result remove_mesh_renderer(scene_node_id node);
  [[nodiscard]] gneiss::engine::result destroy_node(scene_node_id node);
  /** 事务式替换普通场景节点；当前不接受 Prefab 实例容器变化。 */
  [[nodiscard]] gneiss::engine::result reload(gneiss::engine::scene_description candidate);
  /** 将指定 Prefab URI 的新修订应用到当前 Scene 内全部同源实例。 */
  [[nodiscard]] gneiss::engine::result reload_prefab(std::string_view uri,
                                                     prefab_asset_ref candidate);

  /** 单场景 Prefab 实例操作；服务已校验线程与句柄，不修改 Prefab 源资源。 */
  [[nodiscard]] gneiss::engine::result
  capture_prefab_instance_header(std::string_view uuid,
                                 gneiss::engine::prefab_instance_description& author,
                                 gneiss::engine::prefab_asset_ref& source) const noexcept;
  [[nodiscard]] gneiss::engine::result
  capture_prefab_instance_binding(std::string_view uuid, std::size_t index,
                                  scene_mesh_binding& output) const noexcept;
  [[nodiscard]] result instantiate_prefab_ready(const prefab_instance_description& author,
                                                prefab_asset_ref prefab,
                                                std::span<const scene_mesh_binding> bindings,
                                                bool replacing, scene_node_id& output) noexcept;
  [[nodiscard]] result remove_prefab_instance(std::string_view uuid) noexcept;

  /** 私有批量替换事务，只能在所属线程同一次无重入调用内准备和提交。
   * Scene 和节点存储必须存活且保持稳定；放弃事务不修改绑定。 */
  /** 所属线程收集普通节点与 Prefab 的资源引用；追加至事务临时数组，分配失败向调用者传播。
   * 不去重、不发布资源，也不改变场景；服务负责跨实例去重和失败输出保留。 */

  /** 调用方服务已验证所属线程和租约归属；实例负责作者数据与 ECS 同步提交。 */
  [[nodiscard]] gneiss::engine::result
  capture_mesh_binding(scene_node_id node, scene_mesh_binding& output) const noexcept;
  [[nodiscard]] gneiss::engine::result
  replace_mesh_binding(scene_node_id node, const scene_mesh_binding& expected,
                       const scene_mesh_binding& replacement) noexcept;
  [[nodiscard]] bool contains_editable_node(scene_node_id node) const noexcept;
  [[nodiscard]] std::uint64_t node_count() const noexcept { return objects_.size(); }

  [[nodiscard]] const gneiss::engine::scene_description& source() const noexcept {
    return description_;
  }
  [[nodiscard]] scene_node_id staged_node(std::size_t index) const {
    return objects_.at(index).node;
  }
  void append_staged_prefab(std::unique_ptr<prefab_runtime_instance> candidate);
  [[nodiscard]] gneiss::engine::result initialize(gneiss::engine::scene_description candidate);

private:
  friend class scene_instances;
  [[nodiscard]] gneiss::engine::result
  stage_assets(const gneiss::engine::scene_description& source);
  [[nodiscard]] gneiss::engine::result
  commit_scene(const gneiss::engine::scene_description& source);
  [[nodiscard]] gneiss::engine::result
  commit_prefabs(const gneiss::engine::scene_description& source);
  std::vector<object> objects_;
  std::vector<std::unique_ptr<prefab_runtime_instance>> prefab_instances_;
  gneiss::engine::scene_description description_;

  struct prefab_refresh_transaction final {
    prefab_refresh_token token = {};
    std::string instance_uuid;
    gneiss::engine::prefab_asset_ref alternate;
  };

  gneiss::engine::world& native_world_;
  gneiss::engine::render_asset_loader& loader_;
  gneiss::engine::meta::registry_ref registry_;
  std::vector<prefab_refresh_transaction> prefab_refresh_transactions_;
  std::uint64_t next_prefab_refresh_token_ = 1U;
};

}
