// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCES_HPP_
#define GNEISS_SCENE_SCENE_INSTANCES_HPP_

#include "engine/core/reflection/property_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/scene/prefab_candidate.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/function/scene/scene_query.hpp"
#include "engine/resource/types/prefab_description.hpp"
#include "engine/resource/types/scene_description.hpp"

#include "engine/function/world/world.hpp"

#include "engine/core/rid.hpp"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

class scene_instance;
struct prepared_scene_description;

/** 单个 World 的实例集合，拥有实例与 RID 表；不负责后台加载或活动域切换。 */
class GNEISS_API scene_instances final {
public:
  using mesh_binding = scene_mesh_binding;
  using asset_bindings = std::map<std::string, gneiss::engine::render_asset_ref, std::less<>>;
  /** 所属线程同步创建整个实例；只消费已准备描述和资源引用，不读取文件。
   * 失败回收已创建节点并清空输出；调用期间不支持暂停或取消。 */
  [[nodiscard]] gneiss::engine::result instantiate_prepared(prepared_scene_description prepared,
                                                            const asset_bindings& assets,
                                                            gneiss::engine::rid& output) noexcept;
  /** 所属线程预留作者容器并验证身份；不读取资产，不发布候选。
   * output 非空时拒绝覆盖；构造失败仍可能交还待清理候选。 */
  [[nodiscard]] gneiss::engine::result begin_prefab_candidate(
      gneiss::engine::rid instance, const gneiss::engine::prefab_instance_description& author,
      gneiss::engine::prefab_asset_ref prefab, gneiss::engine::prefab_description prepared,
      prefab_candidate& output, bool replacing = false) noexcept;
  /** 已预留容量、调用方已准备镜像和历史后提交。成功消耗候选，失败保留。 */
  [[nodiscard]] gneiss::engine::result
  commit_prefab_candidate(gneiss::engine::rid instance, prefab_candidate& candidate) noexcept;
  /** 整批失败不修改场景；成功后各候选接收退休旧实例，由调用方分步清理。 */
  [[nodiscard]] gneiss::engine::result
  commit_prefab_replacements(gneiss::engine::rid instance,
                             std::span<prefab_candidate* const> replacements) noexcept;
  [[nodiscard]] gneiss::engine::result prepare_prefab_removal(gneiss::engine::rid instance,
                                                              std::string_view uuid,
                                                              bool& complete) noexcept;
  [[nodiscard]] gneiss::engine::result commit_prefab_removal(gneiss::engine::rid instance,
                                                             std::string_view uuid,
                                                             prefab_candidate& candidate) noexcept;
  /** 创建仅消费已就绪租约，不加载；失败不留下节点。 */
  [[nodiscard]] gneiss::engine::result
  create_mesh_renderer_node_ready(gneiss::engine::rid instance,
                                  const mesh_renderer_node_creation& desc,
                                  const mesh_binding& prepared, scene_node_id& output) noexcept;
  /** 捕获实际版本租约；所属线程，不读文件。租约须早于渲染服务释放。 */
  [[nodiscard]] gneiss::engine::result capture_mesh_binding(gneiss::engine::rid instance,
                                                            scene_node_id node,
                                                            mesh_binding& output) const noexcept;
  /** 仅提交就绪租约；校验旧租约及 ECS，失败保留原描述和资源，不加载资产。 */
  [[nodiscard]] gneiss::engine::result
  replace_mesh_binding(gneiss::engine::rid instance, scene_node_id node,
                       const mesh_binding& expected, const mesh_binding& replacement) noexcept;
  /** 所属线程采集此 World 普通节点和 Prefab 实际持有的网格、材质租约。
   * 按 RID 去重，使用稳定资源引用；不读文件、不查询最新缓存。
   * 成功替换 output，失败保持 output；快照延长资源寿命，须早于渲染服务释放。
   * 不包含裸 ECS RID 使用者；后续切换仍需验证使用者当前状态。 */
  [[nodiscard]] gneiss::engine::result
  capture_render_assets(std::vector<gneiss::engine::render_asset_ref>& output) noexcept;
  /** 验证普通作者节点归属，不接受 Prefab 的只读来源节点。 */
  [[nodiscard]] gneiss::engine::result validate_editable_node(gneiss::engine::rid instance,
                                                              scene_node_id node) const noexcept;
  scene_instances(gneiss::engine::world& native_world, gneiss::engine::rid world,
                  gneiss::engine::render_asset_loader& loader) noexcept;
  ~scene_instances() noexcept;

  [[nodiscard]] bool is_valid() const noexcept {
    return domain_ != 0U && static_cast<bool>(registry_);
  }
  [[nodiscard]] gneiss::engine::result load(std::string_view uri,
                                            gneiss::engine::rid* out_instance) noexcept;
  [[nodiscard]] gneiss::engine::result reload(gneiss::engine::rid instance,
                                              std::string_view uri) noexcept;
  [[nodiscard]] gneiss::engine::result reload_prefab(gneiss::engine::rid instance,
                                                     std::string_view uri) noexcept;
  [[nodiscard]] gneiss::engine::result create_empty(std::string_view scene_uuid,
                                                    gneiss::engine::rid* out_instance) noexcept;
  [[nodiscard]] gneiss::engine::result unload(gneiss::engine::rid instance) noexcept;
  [[nodiscard]] gneiss::engine::result find_node(gneiss::engine::rid instance,
                                                 std::string_view uuid,
                                                 scene_node_id& out_node) const noexcept;
  /** 采集当前作者描述及运行期变换；成功才替换输出，不编码或读取文件。 */
  [[nodiscard]] gneiss::engine::result
  capture_description(gneiss::engine::rid instance, scene_description& output) const noexcept;
  [[nodiscard]] gneiss::engine::result serialize(gneiss::engine::rid instance,
                                                 std::string& out_json) const noexcept;
  [[nodiscard]] gneiss::engine::result get_node_count(gneiss::engine::rid instance,
                                                      std::uint64_t* out_count) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_node_info(gneiss::engine::rid instance, std::uint64_t index, scene_node_view& out_info,
                node_query_detail detail = node_query_detail::components) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_prefab_node_count(gneiss::engine::rid instance, std::uint64_t* out_count) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_prefab_node_info(gneiss::engine::rid instance, std::uint64_t index,
                       prefab_node_view& out_info) const noexcept;
  /** 捕获实际实例版本与作者覆盖，不读文件；所有输出仅在成功时替换。 */
  [[nodiscard]] gneiss::engine::result
  capture_prefab_header(gneiss::engine::rid instance, std::string_view uuid,
                        gneiss::engine::prefab_instance_description& author,
                        gneiss::engine::prefab_asset_ref& source) const noexcept;
  /** 按来源索引捕获实际资源租约，允许调用方分帧构建历史快照。 */
  [[nodiscard]] gneiss::engine::result capture_prefab_binding(gneiss::engine::rid instance,
                                                              std::string_view uuid,
                                                              std::size_t index,
                                                              mesh_binding& output) const noexcept;
  [[nodiscard]] gneiss::engine::result create_prefab_instance(gneiss::engine::rid instance,
                                                              const prefab_creation& desc,
                                                              scene_node_id& out_root) noexcept;
  [[nodiscard]] gneiss::engine::result set_prefab_instance_name(gneiss::engine::rid instance,
                                                                scene_node_id root,
                                                                std::string_view name) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_prefab_source_transform(gneiss::engine::rid instance, scene_node_id node,
                              const gneiss::engine::transform& transform) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_prefab_instance(gneiss::engine::rid instance,
                                                               scene_node_id root) noexcept;
  [[nodiscard]] gneiss::engine::result
  refresh_prefab_instance(gneiss::engine::rid instance, scene_node_id root,
                          scene_node_id& out_new_root, gneiss::engine::rid* out_token) noexcept;
  [[nodiscard]] gneiss::engine::result toggle_prefab_refresh(gneiss::engine::rid instance,
                                                             gneiss::engine::rid token,
                                                             scene_node_id& out_new_root) noexcept;
  [[nodiscard]] gneiss::engine::result release_prefab_refresh(gneiss::engine::rid instance,
                                                              gneiss::engine::rid token) noexcept;
  [[nodiscard]] gneiss::engine::result create_node(gneiss::engine::rid instance,
                                                   const node_creation& desc,
                                                   scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_node_name(gneiss::engine::rid instance, scene_node_id node, std::string_view name) noexcept;
  [[nodiscard]] gneiss::engine::result
  reparent_node(gneiss::engine::rid instance, scene_node_id node, scene_node_id parent) noexcept;
  [[nodiscard]] gneiss::engine::result capture_subtree(gneiss::engine::rid instance,
                                                       scene_node_id root,
                                                       std::string& out_snapshot) const noexcept;
  [[nodiscard]] gneiss::engine::result
  restore_subtree(gneiss::engine::rid instance, std::string_view snapshot, scene_node_id parent,
                  std::span<const uuid_mapping> mappings, scene_node_id& out_root) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_subtree(gneiss::engine::rid instance,
                                                       scene_node_id root) noexcept;
  [[nodiscard]] gneiss::engine::result
  create_mesh_renderer_node(gneiss::engine::rid instance, const mesh_renderer_node_creation& desc,
                            scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result set_mesh_renderer(gneiss::engine::rid instance,
                                                         scene_node_id node,
                                                         std::string_view mesh_uri,
                                                         std::string_view material_uri) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_camera(gneiss::engine::rid instance, scene_node_id node,
             const gneiss::engine::camera_description& desc) noexcept;
  [[nodiscard]] gneiss::engine::result remove_camera(gneiss::engine::rid instance,
                                                     scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result remove_mesh_renderer(gneiss::engine::rid instance,
                                                            scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_node(gneiss::engine::rid instance,
                                                    scene_node_id node) noexcept;

private:
  using instance_ptr = std::unique_ptr<scene_instance>;
  gneiss::engine::world& native_world_;
  // 仅供原生反射访问器定位；不拥有 World，不用于普通场景操作。
  gneiss::engine::rid reflection_context_;
  gneiss::engine::render_asset_loader& loader_;
  gneiss::engine::registry_ref registry_;
  std::uint16_t domain_;
  gneiss::engine::rid_table<instance_ptr> instances_;
};

} // namespace gneiss::engine

#endif
