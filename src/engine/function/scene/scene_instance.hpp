// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/function/scene/scene_instance_service.hpp"

namespace gneiss::scene_internal {
class scene_instance final {
public:
  scene_instance(engine::world& native_world, gneiss_world world,
                 render_internal::render_asset_loader& loader, prefab_asset_loader& prefab_loader,
                 gneiss_type_registry registry) noexcept;
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
  /** 仅用于不可见候选 World，分阶段入口不执行文件读取。 */
  void initialize_staged(scene_description candidate);
  [[nodiscard]] gneiss_result create_staged_node(std::size_t index, gneiss_scene_node_id parent,
                                                 render_internal::mesh_asset_lease mesh,
                                                 render_internal::material_asset_lease material);

  [[nodiscard]] gneiss_scene_node_id find_node(std::string_view uuid) const noexcept;
  [[nodiscard]] gneiss_result serialize(std::string& out_json) const;
  [[nodiscard]] gneiss_result get_node_info(std::uint64_t index, scene_node_view& out_info,
                                            node_query_detail detail) const;
  [[nodiscard]] std::uint64_t get_prefab_node_count() const noexcept;
  [[nodiscard]] gneiss_result get_prefab_node_info(std::uint64_t index,
                                                   prefab_node_view& out_info) const;
  [[nodiscard]] gneiss_result create_prefab_instance(const prefab_creation& desc,
                                                     gneiss_scene_node_id* out_root);
  [[nodiscard]] gneiss_result set_prefab_instance_name(gneiss_scene_node_id root,
                                                       std::string_view name);
  [[nodiscard]] gneiss_result
  set_prefab_source_transform(gneiss_scene_node_id node,
                              const gneiss::engine::transform& transform);
  [[nodiscard]] gneiss_result destroy_prefab_instance(gneiss_scene_node_id root) noexcept;
  [[nodiscard]] gneiss_result refresh_prefab_instance(gneiss_scene_node_id root,
                                                      gneiss_scene_node_id* out_new_root,
                                                      gneiss_scene_prefab_refresh_token* out_token);
  [[nodiscard]] gneiss_result toggle_prefab_refresh(gneiss_scene_prefab_refresh_token token,
                                                    gneiss_scene_node_id* out_new_root);
  [[nodiscard]] gneiss_result
  release_prefab_refresh(gneiss_scene_prefab_refresh_token token) noexcept;
  [[nodiscard]] gneiss_result create_node(const node_creation& desc,
                                          gneiss_scene_node_id* out_node);
  [[nodiscard]] gneiss_result set_node_name(gneiss_scene_node_id node, std::string_view name);
  [[nodiscard]] gneiss_result reparent_node(gneiss_scene_node_id node, gneiss_scene_node_id parent);
  [[nodiscard]] gneiss_result capture_subtree(gneiss_scene_node_id root,
                                              std::string& out_snapshot) const;
  [[nodiscard]] gneiss_result restore_subtree(std::string_view snapshot,
                                              gneiss_scene_node_id parent,
                                              std::span<const uuid_mapping> mappings,
                                              gneiss_scene_node_id* out_root);
  [[nodiscard]] gneiss_result destroy_subtree(gneiss_scene_node_id root);
  [[nodiscard]] gneiss_result
  create_mesh_renderer_node(const mesh_renderer_node_creation& desc, gneiss_scene_node_id* out_node,
                            const scene_mesh_binding* prepared = nullptr);
  [[nodiscard]] gneiss_result set_mesh_renderer(gneiss_scene_node_id node,
                                                std::string_view mesh_uri,
                                                std::string_view material_uri);
  [[nodiscard]] gneiss_result set_camera(gneiss_scene_node_id node, const camera_description& desc);
  [[nodiscard]] gneiss_result remove_camera(gneiss_scene_node_id node);
  [[nodiscard]] gneiss_result remove_mesh_renderer(gneiss_scene_node_id node);
  [[nodiscard]] gneiss_result destroy_node(gneiss_scene_node_id node);
  /** 事务式替换普通场景节点；当前不接受 Prefab 实例容器变化。 */
  [[nodiscard]] gneiss_result reload(scene_description candidate);
  /** 将指定 Prefab URI 的新修订应用到当前 Scene 内全部同源实例。 */
  [[nodiscard]] gneiss_result reload_prefab(std::string_view uri);

  /** 单场景 Prefab 实例事务；服务已校验线程与句柄，不修改 Prefab 源资源。
   * instance 参数仅用于候选所属场景校验，不拥有或重新解析公共句柄。 */
  [[nodiscard]] gneiss_result
  capture_prefab_instance_header(std::string_view uuid, prefab_instance_description& author,
                                 prefab_asset_lease& source) const noexcept;
  [[nodiscard]] gneiss_result
  capture_prefab_instance_binding(std::string_view uuid, std::size_t index,
                                  scene_mesh_binding& output) const noexcept;
  [[nodiscard]] gneiss_result
  prepare_prefab_instance_candidate(gneiss_scene_instance instance,
                                    const prefab_instance_description& author,
                                    prefab_asset_lease prefab, const prefab_description& prepared,
                                    prefab_candidate& output, bool replacing) noexcept;
  [[nodiscard]] gneiss_result
  publish_prefab_instance_candidate(gneiss_scene_instance instance,
                                    prefab_candidate& candidate) noexcept;
  [[nodiscard]] gneiss_result
  replace_prefab_instance_versions(gneiss_scene_instance instance,
                                   std::span<prefab_candidate* const> replacements) noexcept;
  [[nodiscard]] gneiss_result prepare_prefab_instance_removal(std::string_view uuid,
                                                              bool& complete) noexcept;
  [[nodiscard]] gneiss_result remove_prefab_instance(std::string_view uuid,
                                                     prefab_candidate& candidate) noexcept;

  /** 所属线程收集普通节点与 Prefab 的资源引用；追加至事务临时数组，分配失败向调用者传播。
   * 不去重、不发布资源，也不改变场景；服务负责跨实例去重和失败输出保留。 */
  void append_render_assets(std::vector<render_internal::render_asset_lease>& output) const;

  /** 调用方服务已验证所属线程和租约归属；实例负责作者数据与 ECS 同步提交。 */
  [[nodiscard]] gneiss_result capture_mesh_binding(scene_node_id node,
                                                   scene_mesh_binding& output) const noexcept;
  [[nodiscard]] gneiss_result replace_mesh_binding(scene_node_id node,
                                                   const scene_mesh_binding& expected,
                                                   const scene_mesh_binding& replacement) noexcept;
  [[nodiscard]] bool contains_editable_node(scene_node_id node) const noexcept;
  [[nodiscard]] std::uint64_t node_count() const noexcept { return objects_.size(); }

  [[nodiscard]] const scene_description& source() const noexcept { return description_; }
  [[nodiscard]] gneiss_scene_node_id staged_node(std::size_t index) const {
    return objects_.at(index).node;
  }
  void append_staged_prefab(std::unique_ptr<prefab_runtime_instance> candidate);
  [[nodiscard]] gneiss_result initialize(scene_description candidate);

private:
  friend class scene_instance_service;
  [[nodiscard]] gneiss_result stage_assets(const scene_description& source);
  [[nodiscard]] gneiss_result commit_scene(const scene_description& source);
  [[nodiscard]] gneiss_result commit_prefabs(const scene_description& source);
  std::vector<object> objects_;
  std::vector<std::unique_ptr<prefab_runtime_instance>> prefab_instances_;
  scene_description description_;

  struct prefab_refresh_transaction final {
    gneiss_scene_prefab_refresh_token token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
    std::string instance_uuid;
    prefab_asset_lease alternate;
  };

  engine::world& native_world_;
  // 仅供 ABI 反射回调定位；不拥有 World，不用于普通场景操作。
  gneiss_world reflection_context_;
  render_internal::render_asset_loader& loader_;
  prefab_asset_loader& prefab_loader_;
  gneiss_type_registry registry_;
  std::vector<prefab_refresh_transaction> prefab_refresh_transactions_;
  gneiss_scene_prefab_refresh_token next_prefab_refresh_token_ = 1U;
};

}
