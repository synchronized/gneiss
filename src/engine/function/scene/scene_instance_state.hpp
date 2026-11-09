// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_types.hpp"
#include <map>

namespace gneiss::engine {
class scene_instance_state final {
public:
  scene_instance_state(world& native_world, asset_manager& loader) noexcept;
  ~scene_instance_state() noexcept;

  scene_instance_state(const scene_instance_state&) = delete;
  scene_instance_state& operator=(const scene_instance_state&) = delete;

  void rollback() noexcept;
  /** 实例槽位创建后绑定身份；成员归属保存在 World 的 ECS 中。 */
  void bind(scene_instance_id id) noexcept { id_ = id; }
  [[nodiscard]] scene_node_id find_node(guid uuid) const;
  [[nodiscard]] result<scene_node_info> get_node_info(std::uint64_t index,
                                                      node_query_detail detail) const;
  [[nodiscard]] result<scene_node_id> create_node(const node_creation& desc);
  [[nodiscard]] error reparent_node(scene_node_id node, scene_node_id parent);
  [[nodiscard]] error destroy_subtree(scene_node_id root);
  [[nodiscard]] result<scene_node_id>
  create_mesh_renderer_node(const mesh_renderer_node_creation& desc,
                            const scene_mesh_binding* prepared = nullptr);
  [[nodiscard]] error set_mesh_renderer(scene_node_id node, std::string_view mesh_uri,
                                        std::string_view material_uri);
  /** 修改运行组件，不生成或修改 Prefab 覆盖。 */
  [[nodiscard]] error add_component_description(scene_node_id node,
                                                const component_description& value,
                                                guid identity = {},
                                                const scene_mesh_binding* prepared = nullptr);
  [[nodiscard]] error remove_component_description(scene_node_id node, guid type);
  /** 替换已有运行组件，不支持新增或移除。 */
  [[nodiscard]] error set_component_description(scene_node_id node,
                                                const component_description& value,
                                                const scene_mesh_binding* prepared = nullptr);
  [[nodiscard]] error set_camera(scene_node_id node, const camera_description& desc);
  [[nodiscard]] error remove_camera(scene_node_id node);
  [[nodiscard]] error remove_mesh_renderer(scene_node_id node);
  [[nodiscard]] error destroy_node(scene_node_id node);
  /** 捕获当前 ECS 引用；替换组件只写 World，不修改来源记录。 */
  [[nodiscard]] result<scene_mesh_binding> capture_mesh_binding(scene_node_id node) const;
  [[nodiscard]] error replace_mesh_binding(scene_node_id node, const scene_mesh_binding& expected,
                                           const scene_mesh_binding& replacement,
                                           guid component_uuid = {});
  [[nodiscard]] bool contains_editable_node(scene_node_id node) const noexcept;
  [[nodiscard]] std::uint64_t node_count() const noexcept;

private:
  friend class scene_instances;
  [[nodiscard]] world_state& storage() const noexcept;
  [[nodiscard]] std::vector<scene_node_id> members() const;
  void adopt(scene_node_id node);
  scene_instance_id id_;

  world& native_world_;
  asset_manager& loader_;
};
}
