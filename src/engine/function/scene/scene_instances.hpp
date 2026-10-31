// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCES_HPP_
#define GNEISS_SCENE_SCENE_INSTANCES_HPP_

#include "engine/core/guid.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_mesh_binding.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/scene/scene_query.hpp"
#include "engine/function/scene/scene_subtree_snapshot.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include "engine/function/world/world.hpp"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine {

/** 单个 Prefab 组件的编辑快照；历史持有资源强引用，描述不是运行时权威副本。 */
struct prefab_component_state {
  std::optional<component_description> added{};
  bool removed = false;
  std::vector<prefab_property_override> properties{};
  scene_mesh_binding resources{};
};
class scene_instance;
/** 捕获的实例化输入与资源强引用；不读取文件。 */
struct scene_capture {
  scene_instantiation_data description;
  std::map<std::string, render_resource_ref, std::less<>> assets;
};
/** Prefab 来源描述与强引用的成组快照。 */
struct prefab_header_snapshot {
  object_description author;
  prefab_asset_ref source;
};

/** 单个 World 的实例集合，拥有实例与 RID 表；不负责后台加载或活动域切换。 */
class GNEISS_API scene_instances final {
public:
  using resource_bindings = std::map<std::string, render_resource_ref, std::less<>>;
  /** 所属线程同步创建整个实例；只消费已准备描述和资源引用，不读取文件。
   * 失败回收已创建节点；调用期间不支持暂停或取消。 */
  [[nodiscard]] result<scene_instance_id>
  instantiate_prepared(scene_instantiation_data prepared, const resource_bindings& assets) noexcept;
  /** 捕获纯内存描述和已加载资源强引用；不读文件，资源尚未准备时返回失败。 */
  [[nodiscard]] result<scene_capture> capture_prepared(scene_instance_id instance) const noexcept;
  /** 同步实例化；替换先移除旧实例，失败只清理本次新建节点，不恢复旧实例。 */
  [[nodiscard]] result<scene_node_id>
  instantiate_prefab_ready(scene_instance_id instance, const object_description& author,
                           prefab_asset_ref prefab, std::span<const scene_mesh_binding> bindings,
                           bool replacing) noexcept;
  [[nodiscard]] error remove_prefab_instance(scene_instance_id instance, guid uuid) noexcept;
  /** 创建仅消费已加载资源引用，不加载；失败不留下节点。 */
  [[nodiscard]] result<scene_node_id>
  create_mesh_renderer_node_ready(scene_instance_id instance,
                                  const mesh_renderer_node_creation& desc,
                                  const scene_mesh_binding& prepared) noexcept;
  /** 捕获当前资源强引用；所属线程，不读文件。引用须早于渲染服务释放。 */
  [[nodiscard]] result<scene_mesh_binding> capture_mesh_binding(scene_instance_id instance,
                                                                scene_node_id node) const noexcept;
  /** 仅提交资源引用；expected 为空时恢复已移除组件，不加载资产。
   * 替换既有组件时校验旧引用及 ECS，失败保留原描述和资源。Prefab 只更新实例 URI 覆盖。 */
  [[nodiscard]] error replace_mesh_binding(scene_instance_id instance, scene_node_id node,
                                           const scene_mesh_binding& expected,
                                           const scene_mesh_binding& replacement) noexcept;
  /** 验证普通作者节点或 Prefab 来源节点归属；组件增删仍受各接口限制。 */
  [[nodiscard]] error validate_editable_node(scene_instance_id instance,
                                             scene_node_id node) const noexcept;
  scene_instances(world& native_world, resource_service& loader) noexcept;
  ~scene_instances() noexcept;

  [[nodiscard]] bool is_valid() const noexcept {
    const auto frozen = meta::is_frozen();
    return domain_ != 0U && frozen && *frozen;
  }
  /** 应用已经加载的描述；调用方先准备渲染依赖，保持现有节点身份。 */
  [[nodiscard]] error reload_loaded(scene_instance_id instance,
                                    scene_description description) noexcept;
  [[nodiscard]] error reload_prefab_loaded(scene_instance_id instance, std::string_view uri,
                                           prefab_asset_ref candidate,
                                           scene_diagnostic* diagnostic = nullptr) noexcept;
  [[nodiscard]] result<scene_instance_id> create_empty(guid scene_uuid) noexcept;
  [[nodiscard]] error unload(scene_instance_id instance) noexcept;
  [[nodiscard]] result<scene_node_id> find_node(scene_instance_id instance,
                                                guid uuid) const noexcept;
  /** 采集当前作者描述及运行期变换；成功才返回完整描述，不编码或读取文件。 */
  [[nodiscard]] result<scene_description>
  capture_description(scene_instance_id instance) const noexcept;
  [[nodiscard]] result<std::string> serialize(scene_instance_id instance) const noexcept;
  [[nodiscard]] result<std::uint64_t> get_node_count(scene_instance_id instance) const noexcept;
  [[nodiscard]] result<scene_node_info>
  get_node_info(scene_instance_id instance, std::uint64_t index,
                node_query_detail detail = node_query_detail::components) const noexcept;
  /** 捕获当前 Prefab 来源描述与属性覆盖，不读文件；成功才返回完整快照。 */
  [[nodiscard]] result<prefab_header_snapshot> capture_prefab_header(scene_instance_id instance,
                                                                     guid uuid) const noexcept;
  /** 按来源索引捕获当前资源强引用，允许调用方分帧构建历史快照。 */
  [[nodiscard]] result<scene_mesh_binding>
  capture_prefab_binding(scene_instance_id instance, guid uuid, std::size_t index) const noexcept;
  [[nodiscard]] error set_prefab_source_transform(scene_instance_id instance, scene_node_id node,
                                                  const math::transform& transform) noexcept;
  [[nodiscard]] result<scene_node_id> create_node(scene_instance_id instance,
                                                  const node_creation& desc) noexcept;
  /** 修改显式对象名称，普通对象与 Prefab 根共用；来源内部节点不支持独立重命名。 */
  [[nodiscard]] error set_node_name(scene_instance_id instance, scene_node_id node,
                                    std::string_view name) noexcept;
  [[nodiscard]] error reparent_node(scene_instance_id instance, scene_node_id node,
                                    scene_node_id parent) noexcept;
  [[nodiscard]] result<scene_subtree_snapshot> capture_subtree(scene_instance_id instance,
                                                               scene_node_id root) const noexcept;
  /** 只消费内存描述和资源强引用；失败回收本次创建的节点，不读取文件。 */
  [[nodiscard]] result<scene_node_id>
  restore_subtree(scene_instance_id instance, const scene_subtree_snapshot& snapshot,
                  scene_node_id parent, std::span<const uuid_mapping> mappings) noexcept;
  /** 删除整棵自有子树或 Prefab 实例，统一清理节点、实体和来源关系。 */
  [[nodiscard]] error destroy_subtree(scene_instance_id instance, scene_node_id root) noexcept;
  [[nodiscard]] result<scene_node_id>
  create_mesh_renderer_node(scene_instance_id instance,
                            const mesh_renderer_node_creation& desc) noexcept;
  [[nodiscard]] error set_mesh_renderer(scene_instance_id instance, scene_node_id node,
                                        std::string_view mesh_uri,
                                        std::string_view material_uri) noexcept;
  /** 单个来源组件的覆盖快照；包含撤销需要的实际资源引用，调用限所属线程。 */
  [[nodiscard]] result<prefab_component_state>
  capture_component_override(scene_instance_id instance, scene_node_id node, guid type) noexcept;
  [[nodiscard]] error set_component_override(scene_instance_id instance, scene_node_id node,
                                             guid type,
                                             const prefab_component_state& state) noexcept;
  /** 普通对象修改 ECS；Prefab 节点写结构覆盖。资源必须已经准备。 */
  [[nodiscard]] error add_component_description(scene_instance_id instance, scene_node_id node,
                                                const component_description& value) noexcept;
  [[nodiscard]] error remove_component_description(scene_instance_id instance, scene_node_id node,
                                                   guid type) noexcept;
  /** 替换已有组件；Prefab 同时更新稀疏覆盖，不支持新增或移除。 */
  [[nodiscard]] error set_component_description(scene_instance_id instance, scene_node_id node,
                                                const component_description& value) noexcept;
  [[nodiscard]] error set_camera(scene_instance_id instance, scene_node_id node,
                                 const camera_description& desc) noexcept;
  [[nodiscard]] error remove_camera(scene_instance_id instance, scene_node_id node) noexcept;
  [[nodiscard]] error remove_mesh_renderer(scene_instance_id instance, scene_node_id node) noexcept;
  /** 只删除无实际子节点的显式对象；存在子节点返回 invalid_state。 */
  [[nodiscard]] error destroy_node(scene_instance_id instance, scene_node_id node) noexcept;

private:
  using instance_ptr = std::unique_ptr<scene_instance>;
  world& native_world_;
  resource_service& loader_;
  std::uint16_t domain_;
  rid_table<instance_ptr, scene_instance_id> instances_;
};

} // namespace gneiss::engine

#endif
