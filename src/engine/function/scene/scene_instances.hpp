// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_SCENE_SCENE_INSTANCES_HPP_
#define GNEISS_SCENE_SCENE_INSTANCES_HPP_

#include "engine/core/guid.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid_table.hpp"
#include "engine/function/scene/scene_creation.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/scene/scene_query.hpp"
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

class scene_instance_state;
/** 已创建的场景及其视觉加载清单；资源身份已预留，内容加载由调用方调度。 */
struct scene_instantiation_result {
  scene_instance_id instance;
  std::size_t node_count{};
  std::vector<scene_object_binding> objects;
  std::vector<render_resource_source> assets;
  std::map<std::string, render_resource_ref, std::less<>> bindings;
};
/** 单个 World 的实例集合，拥有实例与 RID 表；不负责后台加载或活动域切换。 */
class scene_instances final {
public:
  using resource_bindings = std::map<std::string, render_resource_ref, std::less<>>;
  /** 所属线程同步创建整个实例；只消费已准备描述和资源引用，不读取文件。
   * 失败回收已创建节点；调用期间不支持暂停或取消。 */
  [[nodiscard]] GNEISS_API result<scene_instantiation_result>
  instantiate_prepared(scene_instantiation_data prepared, const resource_bindings& assets);
  /** 重建投影并保留实例句柄；成功后旧节点句柄失效，失败保留旧投影。 */
  [[nodiscard]] GNEISS_API result<scene_instantiation_result>
  replace_prepared(scene_instance_id, scene_instantiation_data, const resource_bindings&);
  /** 验证运行节点的场景归属。 */
  [[nodiscard]] GNEISS_API error validate_editable_node(scene_instance_id instance,
                                                        scene_node_id node) const noexcept;
  /** 初始化 World 唯一场景集合；返回借用指针，重复初始化拒绝。不借用资产服务。 */
  [[nodiscard]] static GNEISS_API result<scene_instances*> attach(world&);
  GNEISS_API ~scene_instances() noexcept;

  [[nodiscard]] bool is_valid() const noexcept {
    const auto frozen = meta::is_frozen();
    return domain_ != 0U && frozen && *frozen;
  }
  [[nodiscard]] GNEISS_API result<scene_instance_id> create_empty(guid scene_uuid);
  [[nodiscard]] GNEISS_API error unload(scene_instance_id instance) noexcept;
  [[nodiscard]] GNEISS_API result<scene_node_id> find_node(scene_instance_id instance,
                                                           guid uuid) const;
  [[nodiscard]] GNEISS_API result<std::uint64_t> get_node_count(scene_instance_id instance) const;
  [[nodiscard]] GNEISS_API result<scene_node_info>
  get_node_info(scene_instance_id instance, std::uint64_t index,
                node_query_detail detail = node_query_detail::components) const;
  [[nodiscard]] GNEISS_API result<scene_node_id> create_node(scene_instance_id instance,
                                                             const node_creation& desc);
  [[nodiscard]] GNEISS_API error reparent_node(scene_instance_id instance, scene_node_id node,
                                               scene_node_id parent);
  /** 删除整棵运行子树，统一清理节点与实体。 */
  [[nodiscard]] GNEISS_API error destroy_subtree(scene_instance_id instance, scene_node_id root);
  /** 只删除无实际子节点的显式对象；存在子节点返回 invalid_state。 */
  [[nodiscard]] GNEISS_API error destroy_node(scene_instance_id instance, scene_node_id node);

private:
  friend class world;
  [[nodiscard]] result<scene_instance_state*> resolve(scene_instance_id) const noexcept;
  GNEISS_API scene_instances(world& native_world) noexcept;
  using instance_ptr = std::unique_ptr<scene_instance_state>;
  world& native_world_;
  std::uint16_t domain_;
  rid_table<instance_ptr, scene_instance_id> instances_;
};

} // namespace gneiss::engine

#endif
