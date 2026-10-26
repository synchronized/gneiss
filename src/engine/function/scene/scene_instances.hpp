// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCES_HPP_
#define GNEISS_SCENE_SCENE_INSTANCES_HPP_

#include "engine/core/meta/type_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/function/scene/scene_query.hpp"
#include "engine/resource/types/prefab_description.hpp"
#include "engine/resource/types/scene_description.hpp"

#include "engine/function/world/world.hpp"

#include "engine/core/scene_types.hpp"

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
                                                            scene_instance_id& output) noexcept;
  /** 捕获纯内存描述和已加载资源强引用；不读文件，资源尚未准备时返回失败。 */
  [[nodiscard]] result capture_prepared(scene_instance_id instance,
                                        prepared_scene_description& description,
                                        asset_bindings& assets) const noexcept;
  /** 同步实例化；替换先移除旧实例，失败只清理本次新建节点，不恢复旧实例。 */
  [[nodiscard]] result instantiate_prefab_ready(scene_instance_id instance,
                                                const prefab_instance_description& author,
                                                prefab_asset_ref prefab,
                                                std::span<const scene_mesh_binding> bindings,
                                                bool replacing, scene_node_id& output) noexcept;
  [[nodiscard]] result remove_prefab_instance(scene_instance_id instance,
                                              std::string_view uuid) noexcept;
  /** 创建仅消费已就绪租约，不加载；失败不留下节点。 */
  [[nodiscard]] gneiss::engine::result
  create_mesh_renderer_node_ready(scene_instance_id instance,
                                  const mesh_renderer_node_creation& desc,
                                  const mesh_binding& prepared, scene_node_id& output) noexcept;
  /** 捕获实际版本租约；所属线程，不读文件。租约须早于渲染服务释放。 */
  [[nodiscard]] gneiss::engine::result capture_mesh_binding(scene_instance_id instance,
                                                            scene_node_id node,
                                                            mesh_binding& output) const noexcept;
  /** 仅提交就绪租约；校验旧租约及 ECS，失败保留原描述和资源，不加载资产。 */
  [[nodiscard]] gneiss::engine::result
  replace_mesh_binding(scene_instance_id instance, scene_node_id node, const mesh_binding& expected,
                       const mesh_binding& replacement) noexcept;
  /** 验证普通作者节点归属，不接受 Prefab 的只读来源节点。 */
  [[nodiscard]] gneiss::engine::result validate_editable_node(scene_instance_id instance,
                                                              scene_node_id node) const noexcept;
  scene_instances(gneiss::engine::world& native_world,
                  gneiss::engine::render_asset_loader& loader) noexcept;
  ~scene_instances() noexcept;

  [[nodiscard]] bool is_valid() const noexcept {
    return domain_ != 0U && static_cast<bool>(registry_);
  }
  [[nodiscard]] gneiss::engine::result load(std::string_view uri,
                                            scene_instance_id* out_instance) noexcept;
  /** 应用已经加载的描述；调用方先准备渲染依赖，保持现有节点身份。 */
  [[nodiscard]] result reload_loaded(scene_instance_id instance,
                                     scene_description description) noexcept;
  [[nodiscard]] result reload_prefab_loaded(scene_instance_id instance, std::string_view uri,
                                            prefab_asset_ref candidate) noexcept;
  [[nodiscard]] gneiss::engine::result create_empty(std::string_view scene_uuid,
                                                    scene_instance_id* out_instance) noexcept;
  [[nodiscard]] gneiss::engine::result unload(scene_instance_id instance) noexcept;
  [[nodiscard]] gneiss::engine::result find_node(scene_instance_id instance, std::string_view uuid,
                                                 scene_node_id& out_node) const noexcept;
  /** 采集当前作者描述及运行期变换；成功才替换输出，不编码或读取文件。 */
  [[nodiscard]] gneiss::engine::result
  capture_description(scene_instance_id instance, scene_description& output) const noexcept;
  [[nodiscard]] gneiss::engine::result serialize(scene_instance_id instance,
                                                 std::string& out_json) const noexcept;
  [[nodiscard]] gneiss::engine::result get_node_count(scene_instance_id instance,
                                                      std::uint64_t* out_count) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_node_info(scene_instance_id instance, std::uint64_t index, scene_node_view& out_info,
                node_query_detail detail = node_query_detail::components) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_prefab_node_count(scene_instance_id instance, std::uint64_t* out_count) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_prefab_node_info(scene_instance_id instance, std::uint64_t index,
                       prefab_node_view& out_info) const noexcept;
  /** 捕获实际实例版本与作者覆盖，不读文件；所有输出仅在成功时替换。 */
  [[nodiscard]] gneiss::engine::result
  capture_prefab_header(scene_instance_id instance, std::string_view uuid,
                        gneiss::engine::prefab_instance_description& author,
                        gneiss::engine::prefab_asset_ref& source) const noexcept;
  /** 按来源索引捕获实际资源租约，允许调用方分帧构建历史快照。 */
  [[nodiscard]] gneiss::engine::result capture_prefab_binding(scene_instance_id instance,
                                                              std::string_view uuid,
                                                              std::size_t index,
                                                              mesh_binding& output) const noexcept;
  [[nodiscard]] gneiss::engine::result create_prefab_instance(scene_instance_id instance,
                                                              const prefab_creation& desc,
                                                              scene_node_id& out_root) noexcept;
  [[nodiscard]] gneiss::engine::result set_prefab_instance_name(scene_instance_id instance,
                                                                scene_node_id root,
                                                                std::string_view name) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_prefab_source_transform(scene_instance_id instance, scene_node_id node,
                              const gneiss::engine::transform& transform) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_prefab_instance(scene_instance_id instance,
                                                               scene_node_id root) noexcept;
  [[nodiscard]] gneiss::engine::result
  refresh_prefab_instance(scene_instance_id instance, scene_node_id root,
                          scene_node_id& out_new_root, prefab_refresh_token* out_token) noexcept;
  [[nodiscard]] gneiss::engine::result toggle_prefab_refresh(scene_instance_id instance,
                                                             prefab_refresh_token token,
                                                             scene_node_id& out_new_root) noexcept;
  [[nodiscard]] gneiss::engine::result release_prefab_refresh(scene_instance_id instance,
                                                              prefab_refresh_token token) noexcept;
  [[nodiscard]] gneiss::engine::result create_node(scene_instance_id instance,
                                                   const node_creation& desc,
                                                   scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result set_node_name(scene_instance_id instance, scene_node_id node,
                                                     std::string_view name) noexcept;
  [[nodiscard]] gneiss::engine::result reparent_node(scene_instance_id instance, scene_node_id node,
                                                     scene_node_id parent) noexcept;
  [[nodiscard]] gneiss::engine::result capture_subtree(scene_instance_id instance,
                                                       scene_node_id root,
                                                       std::string& out_snapshot) const noexcept;
  [[nodiscard]] gneiss::engine::result
  restore_subtree(scene_instance_id instance, std::string_view snapshot, scene_node_id parent,
                  std::span<const uuid_mapping> mappings, scene_node_id& out_root) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_subtree(scene_instance_id instance,
                                                       scene_node_id root) noexcept;
  [[nodiscard]] gneiss::engine::result
  create_mesh_renderer_node(scene_instance_id instance, const mesh_renderer_node_creation& desc,
                            scene_node_id& out_node) noexcept;
  [[nodiscard]] gneiss::engine::result set_mesh_renderer(scene_instance_id instance,
                                                         scene_node_id node,
                                                         std::string_view mesh_uri,
                                                         std::string_view material_uri) noexcept;
  [[nodiscard]] gneiss::engine::result
  set_camera(scene_instance_id instance, scene_node_id node,
             const gneiss::engine::camera_description& desc) noexcept;
  [[nodiscard]] gneiss::engine::result remove_camera(scene_instance_id instance,
                                                     scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result remove_mesh_renderer(scene_instance_id instance,
                                                            scene_node_id node) noexcept;
  [[nodiscard]] gneiss::engine::result destroy_node(scene_instance_id instance,
                                                    scene_node_id node) noexcept;

private:
  using instance_ptr = std::unique_ptr<scene_instance>;
  gneiss::engine::world& native_world_;
  gneiss::engine::render_asset_loader& loader_;
  gneiss::engine::meta::registry_ref registry_;
  std::uint16_t domain_;
  gneiss::engine::rid_table<instance_ptr, scene_instance_id> instances_;
};

} // namespace gneiss::engine

#endif
