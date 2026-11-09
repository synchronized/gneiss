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

  /** 只记录场景成员；身份在节点中，组件资源在 ECS 中。 */
  struct node_entry final {
    scene_node_id node = null_scene_node_id;
  };

  /** 查询入口清理已经由 World 删除的成员，不缓存存活状态。 */
  void prune_destroyed_members() noexcept;
  void rollback() noexcept;
  /** 同步创建期间预留成员槽位；不保存加载描述、不执行文件读取。 */
  void initialize_staged(guid uuid, std::size_t count = 0);
  [[nodiscard]] scene_node_id find_node(guid uuid) const;
  [[nodiscard]] result<scene_node_info> get_node_info(std::uint64_t index,
                                                      node_query_detail detail) const;
  [[nodiscard]] error set_node_transform(scene_node_id node, const math::transform& transform);
  [[nodiscard]] result<scene_node_id> create_node(const node_creation& desc);
  [[nodiscard]] error set_node_name(scene_node_id node, std::string_view name);
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
  [[nodiscard]] std::uint64_t node_count() const noexcept { return objects_.size(); }

private:
  friend class scene_instances;
  [[nodiscard]] std::vector<node_entry>::iterator find_member(scene_node_id node);
  [[nodiscard]] std::vector<node_entry>::const_iterator find_member(scene_node_id node) const;
  std::vector<node_entry> objects_;
  guid uuid_;

  world& native_world_;
  asset_manager& loader_;
};
}
