// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/world/world.hpp"
#include "engine/resource/scene/scene_description.hpp"

namespace gneiss::engine {
/** 仅用于创建过程；移交场景前的失败路径回收整棵子树。 */
class scene_subtree_guard final {
public:
  scene_subtree_guard(world& owner, scene_node_id root) noexcept : owner_(owner), root_(root) {}
  ~scene_subtree_guard() {
    if (root_)
      (void)owner_.object_destroy(root_);
  }
  scene_subtree_guard(const scene_subtree_guard&) = delete;
  scene_subtree_guard& operator=(const scene_subtree_guard&) = delete;
  void release() noexcept { root_ = {}; }

private:
  world& owner_;
  scene_node_id root_;
};

/** 已验证场景成员的实体查询；无效节点返回零，不缓存实体身份。 */
inline std::uint64_t scene_object_entity(world& owner, scene_node_id node) noexcept {
  entity_id entity;
  (void)owner.node_get_entity(node, entity);
  return entity.get();
}
inline scene_node_identity_view scene_object_identity(world& owner, scene_node_id node) noexcept {
  auto value = owner.node_identity(node);
  return value ? *value : scene_node_identity_view{};
}
/** 当前运行时只安装这两类描述；已注册的其他描述不得被静默忽略。 */
inline error validate_scene_components(const object_description& source) noexcept {
  for (const auto& value : source.components) {
    if (!value.get<camera_description>() && !value.get<mesh_renderer_description>())
      return error::unsupported;
  }
  return source.prefab && !source.components.empty() ? error::invalid_argument : error::success;
}
/** 将已解析组件安装到节点；创建失败的回收由调用者负责。 */
inline error configure_scene_object(world& owner, scene_node_id node,
                                    const object_description& source,
                                    const mesh_renderer& renderer) {
  const auto validation = validate_scene_components(source);
  if (validation.failed())
    return validation;
  const entity_id entity{scene_object_entity(owner, node)};
  auto status = error::success;
  if (source.components.template get<camera_description>()) {
    status = owner.entity_configure_camera(
        entity,
        {source.components.template get<camera_description>()->vertical_field_of_view_radians,
         source.components.template get<camera_description>()->near_plane,
         source.components.template get<camera_description>()->far_plane});
    if (status.ok() && source.components.template get<camera_description>()->is_primary)
      status = owner.set_active_camera(entity);
  }
  if (status.ok() && source.components.template get<mesh_renderer_description>())
    status = owner.entity_set_mesh_renderer(entity, renderer);
  return status;
}
/** 普通对象与 Prefab 共用实例化路径，输入资源仅在组件内长期持有。 */
inline result<scene_node_id> create_scene_object(world& owner, const object_description& source,
                                                 scene_node_id parent,
                                                 const mesh_renderer& renderer) {
  auto created = owner.object_create(parent, {source.uuid, source.name}, source.transform);
  if (!created)
    return created.error();
  const auto status = configure_scene_object(owner, *created, source, renderer);
  if (status.failed()) {
    (void)owner.object_destroy(*created);
    return status;
  }
  return *created;
}
} // namespace gneiss::engine
