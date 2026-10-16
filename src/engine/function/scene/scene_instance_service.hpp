// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCE_SERVICE_HPP_
#define GNEISS_SCENE_SCENE_INSTANCE_SERVICE_HPP_

#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/scene/prefab_asset_loader.hpp"
#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_description.hpp"
#include "engine/function/scene/scene_query.hpp"

#include <gneiss/engine/scene.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::asset_internal {
class virtual_file_system;
}

namespace gneiss::scene_internal {
struct scene_mesh_binding {
  render_internal::render_asset_lease mesh;
  render_internal::render_asset_lease material;
};

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
  using mesh_binding = scene_mesh_binding;
  /** 所属线程预留作者容器并验证身份；不读取资产，不发布候选。 */
  [[nodiscard]] gneiss_result
  begin_prefab_candidate(gneiss_scene_instance instance, const prefab_instance_description& author,
                         prefab_asset_lease prefab, prefab_description prepared,
                         std::unique_ptr<prefab_runtime_instance>& output,
                         bool replacing = false) noexcept;
  /** 已预留容量、调用方已准备镜像和历史后提交。成功接管 runtime。 */
  [[nodiscard]] gneiss_result
  commit_prefab_candidate(gneiss_scene_instance instance, prefab_instance_description& author,
                          std::unique_ptr<prefab_runtime_instance>& runtime) noexcept;
  struct prefab_replacement {
    prefab_instance_description* author{};
    std::unique_ptr<prefab_runtime_instance>* runtime{};
  };
  /** 整批失败不修改场景；成功后 runtime 接收已退休旧实例，由调用方分步清理。 */
  [[nodiscard]] gneiss_result
  commit_prefab_replacements(gneiss_scene_instance instance,
                             std::span<const prefab_replacement> replacements) noexcept;
  [[nodiscard]] gneiss_result prepare_prefab_removal(gneiss_scene_instance instance,
                                                     std::string_view uuid,
                                                     bool& complete) noexcept;
  [[nodiscard]] gneiss_result
  commit_prefab_removal(gneiss_scene_instance instance, std::string_view uuid,
                        std::unique_ptr<prefab_runtime_instance>& retired) noexcept;
  /** 创建仅消费已就绪租约，不加载；失败不留下节点。 */
  [[nodiscard]] gneiss_result create_mesh_renderer_node_ready(
      gneiss_scene_instance instance, const mesh_renderer_node_creation& desc,
      const mesh_binding& prepared, gneiss_scene_node_id& output) noexcept;
  /** 捕获实际版本租约；所属线程，不读文件。租约须早于渲染服务释放。 */
  [[nodiscard]] gneiss_result capture_mesh_binding(gneiss_scene_instance instance,
                                                   gneiss_scene_node_id node,
                                                   mesh_binding& output) const noexcept;
  /** 仅提交就绪租约；校验旧租约及 ECS，失败保留原描述和资源，不加载资产。 */
  [[nodiscard]] gneiss_result replace_mesh_binding(gneiss_scene_instance instance,
                                                   gneiss_scene_node_id node,
                                                   const mesh_binding& expected,
                                                   const mesh_binding& replacement) noexcept;
  /** 所属线程采集此 World 普通节点和 Prefab 实际持有的网格、材质租约。
   * 按 RID 去重，同 URI 的不同版本分别保留；不读文件、不查询最新缓存。
   * 成功替换 output，失败保持 output；快照延长资源寿命，须早于渲染服务释放。
   * 不包含裸 ECS RID 使用者；后续切换仍需验证使用者当前状态。 */
  [[nodiscard]] gneiss_result
  capture_render_assets(std::vector<render_internal::render_asset_lease>& output) noexcept;
  struct asset_replacement {
    render_internal::render_asset_lease previous;
    render_internal::render_asset_lease replacement;
  };
  /** 更新此 World 所有场景和 Prefab 中仍使用 previous RID 的节点。
   * 仅所属线程安全帧边界调用，调用前新资源须已上传；失败不切换租约或 ECS。
   * 原始 RID 使用者不隐式跟随，不改变源描述或实例局部资产选择。 */
  [[nodiscard]] gneiss_result
  replace_render_assets(std::span<const asset_replacement> changes) noexcept;
  /** 验证普通作者节点归属，不接受 Prefab 的只读来源节点。 */
  [[nodiscard]] gneiss_result validate_editable_node(gneiss_scene_instance instance,
                                                     gneiss_scene_node_id node) const noexcept;
  scene_instance_service(gneiss_world world, const asset_internal::virtual_file_system& file_system,
                         render_internal::render_asset_loader& loader,
                         prefab_asset_loader& prefab_loader) noexcept;
  ~scene_instance_service() noexcept;

  [[nodiscard]] bool is_valid() const noexcept {
    return domain_ != 0U && registry_ != GNEISS_NULL_TYPE_REGISTRY;
  }
  [[nodiscard]] gneiss_result load(std::string_view uri,
                                   gneiss_scene_instance* out_instance) noexcept;
  [[nodiscard]] gneiss_result reload(gneiss_scene_instance instance, std::string_view uri) noexcept;
  [[nodiscard]] gneiss_result reload_prefab(gneiss_scene_instance instance,
                                            std::string_view uri) noexcept;
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
  get_node_info(gneiss_scene_instance instance, std::uint64_t index, scene_node_view& out_info,
                node_query_detail detail = node_query_detail::components) const noexcept;
  [[nodiscard]] gneiss_result get_prefab_node_count(gneiss_scene_instance instance,
                                                    std::uint64_t* out_count) const noexcept;
  [[nodiscard]] gneiss_result get_prefab_node_info(gneiss_scene_instance instance,
                                                   std::uint64_t index,
                                                   prefab_node_view& out_info) const noexcept;
  /** 捕获实际实例版本与作者覆盖，不读文件；所有输出仅在成功时替换。 */
  [[nodiscard]] gneiss_result capture_prefab_header(gneiss_scene_instance instance,
                                                    std::string_view uuid,
                                                    prefab_instance_description& author,
                                                    prefab_asset_lease& source) const noexcept;
  /** 按来源索引捕获实际资源租约，允许调用方分帧构建历史快照。 */
  [[nodiscard]] gneiss_result capture_prefab_binding(gneiss_scene_instance instance,
                                                     std::string_view uuid, std::size_t index,
                                                     mesh_binding& output) const noexcept;
  [[nodiscard]] gneiss_result create_prefab_instance(gneiss_scene_instance instance,
                                                     const prefab_creation& desc,
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
  [[nodiscard]] gneiss_result create_node(gneiss_scene_instance instance, const node_creation& desc,
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
  [[nodiscard]] gneiss_result restore_subtree(gneiss_scene_instance instance,
                                              std::string_view snapshot,
                                              gneiss_scene_node_id parent,
                                              std::span<const uuid_mapping> mappings,
                                              gneiss_scene_node_id* out_root) noexcept;
  [[nodiscard]] gneiss_result destroy_subtree(gneiss_scene_instance instance,
                                              gneiss_scene_node_id root) noexcept;
  [[nodiscard]] gneiss_result create_mesh_renderer_node(gneiss_scene_instance instance,
                                                        const mesh_renderer_node_creation& desc,
                                                        gneiss_scene_node_id* out_node) noexcept;
  [[nodiscard]] gneiss_result set_mesh_renderer(gneiss_scene_instance instance,
                                                gneiss_scene_node_id node,
                                                std::string_view mesh_uri,
                                                std::string_view material_uri) noexcept;
  [[nodiscard]] gneiss_result set_camera(gneiss_scene_instance instance, gneiss_scene_node_id node,
                                         const camera_description& desc) noexcept;
  [[nodiscard]] gneiss_result remove_camera(gneiss_scene_instance instance,
                                            gneiss_scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result remove_mesh_renderer(gneiss_scene_instance instance,
                                                   gneiss_scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result destroy_node(gneiss_scene_instance instance,
                                           gneiss_scene_node_id node) noexcept;

private:
  friend class scene_load_builder;
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
