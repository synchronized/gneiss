// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCE_SERVICE_HPP_
#define GNEISS_SCENE_SCENE_INSTANCE_SERVICE_HPP_

#include "engine/resource/scene_description.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/resource/prefab_asset_loader.hpp"
#include "engine/function/scene/prefab_candidate.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/function/scene/scene_query.hpp"

#include "engine/function/world/world.hpp"

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

class scene_instance;

class GNEISS_API scene_instance_service final {
public:
  using mesh_binding = scene_mesh_binding;
  /** 所属线程预留作者容器并验证身份；不读取资产，不发布候选。
   * output 非空时拒绝覆盖；构造失败仍可能交还待清理候选。 */
  [[nodiscard]] gneiss_result
  begin_prefab_candidate(gneiss_scene_instance instance, const prefab_instance_description& author,
                         prefab_asset_lease prefab, prefab_description prepared,
                         prefab_candidate& output, bool replacing = false) noexcept;
  /** 已预留容量、调用方已准备镜像和历史后提交。成功消耗候选，失败保留。 */
  [[nodiscard]] gneiss_result commit_prefab_candidate(gneiss_scene_instance instance,
                                                      prefab_candidate& candidate) noexcept;
  /** 整批失败不修改场景；成功后各候选接收退休旧实例，由调用方分步清理。 */
  [[nodiscard]] gneiss_result
  commit_prefab_replacements(gneiss_scene_instance instance,
                             std::span<prefab_candidate* const> replacements) noexcept;
  [[nodiscard]] gneiss_result prepare_prefab_removal(gneiss_scene_instance instance,
                                                     std::string_view uuid,
                                                     bool& complete) noexcept;
  [[nodiscard]] gneiss_result commit_prefab_removal(gneiss_scene_instance instance,
                                                    std::string_view uuid,
                                                    prefab_candidate& candidate) noexcept;
  /** 创建仅消费已就绪租约，不加载；失败不留下节点。 */
  [[nodiscard]] gneiss_result
  create_mesh_renderer_node_ready(gneiss_scene_instance instance,
                                  const mesh_renderer_node_creation& desc,
                                  const mesh_binding& prepared, scene_node_id& output) noexcept;
  /** 捕获实际版本租约；所属线程，不读文件。租约须早于渲染服务释放。 */
  [[nodiscard]] gneiss_result capture_mesh_binding(gneiss_scene_instance instance,
                                                   scene_node_id node,
                                                   mesh_binding& output) const noexcept;
  /** 仅提交就绪租约；校验旧租约及 ECS，失败保留原描述和资源，不加载资产。 */
  [[nodiscard]] gneiss_result replace_mesh_binding(gneiss_scene_instance instance,
                                                   scene_node_id node, const mesh_binding& expected,
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
                                                     scene_node_id node) const noexcept;
  scene_instance_service(engine::world& native_world, gneiss_world world,
                         const asset_internal::virtual_file_system& file_system,
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
                                        scene_node_id& out_node) const noexcept;
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
                                                     scene_node_id& out_root) noexcept;
  [[nodiscard]] gneiss_result set_prefab_instance_name(gneiss_scene_instance instance,
                                                       scene_node_id root,
                                                       std::string_view name) noexcept;
  [[nodiscard]] gneiss_result
  set_prefab_source_transform(gneiss_scene_instance instance, scene_node_id node,
                              const gneiss::engine::transform& transform) noexcept;
  [[nodiscard]] gneiss_result destroy_prefab_instance(gneiss_scene_instance instance,
                                                      scene_node_id root) noexcept;
  [[nodiscard]] gneiss_result
  refresh_prefab_instance(gneiss_scene_instance instance, scene_node_id root,
                          scene_node_id& out_new_root,
                          gneiss_scene_prefab_refresh_token* out_token) noexcept;
  [[nodiscard]] gneiss_result toggle_prefab_refresh(gneiss_scene_instance instance,
                                                    gneiss_scene_prefab_refresh_token token,
                                                    scene_node_id& out_new_root) noexcept;
  [[nodiscard]] gneiss_result
  release_prefab_refresh(gneiss_scene_instance instance,
                         gneiss_scene_prefab_refresh_token token) noexcept;
  [[nodiscard]] gneiss_result create_node(gneiss_scene_instance instance, const node_creation& desc,
                                          scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss_result set_node_name(gneiss_scene_instance instance, scene_node_id node,
                                            std::string_view name) noexcept;
  [[nodiscard]] gneiss_result reparent_node(gneiss_scene_instance instance, scene_node_id node,
                                            scene_node_id parent) noexcept;
  [[nodiscard]] gneiss_result capture_subtree(gneiss_scene_instance instance, scene_node_id root,
                                              std::string& out_snapshot) const noexcept;
  [[nodiscard]] gneiss_result restore_subtree(gneiss_scene_instance instance,
                                              std::string_view snapshot, scene_node_id parent,
                                              std::span<const uuid_mapping> mappings,
                                              scene_node_id& out_root) noexcept;
  [[nodiscard]] gneiss_result destroy_subtree(gneiss_scene_instance instance,
                                              scene_node_id root) noexcept;
  [[nodiscard]] gneiss_result create_mesh_renderer_node(gneiss_scene_instance instance,
                                                        const mesh_renderer_node_creation& desc,
                                                        scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss_result set_mesh_renderer(gneiss_scene_instance instance, scene_node_id node,
                                                std::string_view mesh_uri,
                                                std::string_view material_uri) noexcept;
  [[nodiscard]] gneiss_result set_camera(gneiss_scene_instance instance, scene_node_id node,
                                         const camera_description& desc) noexcept;
  [[nodiscard]] gneiss_result remove_camera(gneiss_scene_instance instance,
                                            scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result remove_mesh_renderer(gneiss_scene_instance instance,
                                                   scene_node_id node) noexcept;
  [[nodiscard]] gneiss_result destroy_node(gneiss_scene_instance instance,
                                           scene_node_id node) noexcept;

private:
  friend class scene_load_builder;
  using instance_ptr = std::unique_ptr<scene_instance>;
  engine::world& native_world_;
  // 仅供 ABI 反射回调定位；不拥有 World，不用于普通场景操作。
  gneiss_world reflection_context_;
  const asset_internal::virtual_file_system& file_system_;
  render_internal::render_asset_loader& loader_;
  prefab_asset_loader& prefab_loader_;
  gneiss_type_registry registry_ = GNEISS_NULL_TYPE_REGISTRY;
  std::uint16_t domain_;
  core::rid_table<instance_ptr> instances_;
};

} // namespace gneiss::scene_internal

#endif
