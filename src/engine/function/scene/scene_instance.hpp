// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/prefab_binding.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include <map>

namespace gneiss::engine {
class scene_instance final {
public:
  scene_instance(world& native_world, resource_service& loader) noexcept;
  ~scene_instance() noexcept;

  scene_instance(const scene_instance&) = delete;
  scene_instance& operator=(const scene_instance&) = delete;

  /** 只记录场景成员；身份在节点中，组件资源在 ECS 中。 */
  struct object final {
    scene_node_id node = null_scene_node_id;
    bool persistent = true; // Prefab 展开成员仅在运行时枚举，不重复保存。
  };

  void rollback() noexcept;
  /** 同步创建期间预留成员槽位；不保存加载描述、不执行文件读取。 */
  void initialize_staged(guid uuid, std::size_t count = 0);
  [[nodiscard]] error create_staged_node(std::size_t index, const object_description& source,
                                         scene_node_id parent, engine::mesh mesh,
                                         engine::material material);

  [[nodiscard]] scene_node_id find_node(guid uuid) const noexcept;
  [[nodiscard]] error capture_description(scene_description& output) const;
  [[nodiscard]] error serialize(std::string& out_json) const;
  [[nodiscard]] error get_node_info(std::uint64_t index, scene_node_view& out_info,
                                    node_query_detail detail) const;
  [[nodiscard]] error set_prefab_source_transform(scene_node_id node,
                                                  const math::transform& transform);
  [[nodiscard]] error create_node(const node_creation& desc, scene_node_id* out_node);
  [[nodiscard]] error set_node_name(scene_node_id node, std::string_view name);
  [[nodiscard]] error reparent_node(scene_node_id node, scene_node_id parent);
  [[nodiscard]] error capture_subtree(scene_node_id root,
                                      scene_subtree_snapshot& out_snapshot) const;
  [[nodiscard]] error restore_subtree(const scene_subtree_snapshot& snapshot, scene_node_id parent,
                                      std::span<const uuid_mapping> mappings,
                                      scene_node_id* out_root);
  [[nodiscard]] error destroy_subtree(scene_node_id root);
  [[nodiscard]] error create_mesh_renderer_node(const mesh_renderer_node_creation& desc,
                                                scene_node_id* out_node,
                                                const scene_mesh_binding* prepared = nullptr);
  [[nodiscard]] error set_mesh_renderer(scene_node_id node, std::string_view mesh_uri,
                                        std::string_view material_uri);
  /** 替换已有组件；Prefab 同时更新稀疏覆盖，不支持新增或移除。 */
  [[nodiscard]] error set_component_description(scene_node_id node,
                                                const component_description& value) noexcept;
  [[nodiscard]] error set_camera(scene_node_id node, const camera_description& desc);
  [[nodiscard]] error remove_camera(scene_node_id node);
  [[nodiscard]] error remove_mesh_renderer(scene_node_id node);
  [[nodiscard]] error destroy_node(scene_node_id node);
  /** 事务式替换普通场景节点；当前不接受 Prefab 实例容器变化。 */
  [[nodiscard]] error reload(scene_description candidate);
  /** 将指定 Prefab URI 的新修订应用到当前 Scene 内全部同源实例。 */
  [[nodiscard]] error reload_prefab(std::string_view uri, prefab_asset_ref candidate);

  /** 单场景 Prefab 实例操作；服务已校验线程与句柄，不修改 Prefab 源资源。 */
  [[nodiscard]] error capture_prefab_instance_header(guid uuid, object_description& author,
                                                     prefab_asset_ref& source) const noexcept;
  [[nodiscard]] error capture_prefab_instance_binding(guid uuid, std::size_t index,
                                                      scene_mesh_binding& output) const noexcept;
  [[nodiscard]] error instantiate_prefab_ready(const object_description& author,
                                               prefab_asset_ref prefab,
                                               std::span<const scene_mesh_binding> bindings,
                                               bool replacing, scene_node_id& output) noexcept;
  [[nodiscard]] error remove_prefab_instance(guid uuid) noexcept;

  /** 捕获当前 ECS 引用；替换普通组件只写 World，Prefab 同时维护显式覆盖。 */
  [[nodiscard]] error capture_mesh_binding(scene_node_id node,
                                           scene_mesh_binding& output) const noexcept;
  [[nodiscard]] error replace_mesh_binding(scene_node_id node, const scene_mesh_binding& expected,
                                           const scene_mesh_binding& replacement) noexcept;
  [[nodiscard]] bool contains_editable_node(scene_node_id node) const noexcept;
  [[nodiscard]] std::uint64_t node_count() const noexcept { return objects_.size(); }

  [[nodiscard]] scene_node_id staged_node(std::size_t index) const {
    return objects_.at(index).node;
  }
  void append_staged_prefab(std::unique_ptr<prefab_binding> candidate,
                            prefab_reference_description reference, std::size_t index);

private:
  friend class scene_instances;
  [[nodiscard]] std::vector<object>::iterator find_authored_node(scene_node_id node);
  [[nodiscard]] std::vector<object>::iterator find_ordinary_node(scene_node_id node);
  [[nodiscard]] std::vector<object>::const_iterator find_ordinary_node(scene_node_id node) const;
  [[nodiscard]] std::optional<prefab_origin_view> prefab_origin(scene_node_id node) const;
  void append_prefab_members(const prefab_binding& prefab);
  prefab_reference_description& prefab_reference(std::size_t index);
  const prefab_reference_description& prefab_reference(std::size_t index) const;

  std::vector<object> objects_;
  std::vector<std::unique_ptr<prefab_binding>> prefab_instances_;
  guid uuid_;

  world& native_world_;
  resource_service& loader_;
};
}
