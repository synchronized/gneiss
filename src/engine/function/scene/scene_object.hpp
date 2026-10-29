// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/world/world.hpp"
#include "engine/resource/scene/scene_description.hpp"

namespace gneiss::engine {
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
/** 普通对象与 Prefab 共用实例化路径，输入资源仅在组件内长期持有。 */
inline result<scene_node_id> create_scene_object(world& owner, const object_description& source,
                                                 scene_node_id parent,
                                                 const mesh_renderer& renderer) {
  auto created = owner.object_create(parent, {source.uuid, source.name},
                                     {source.translation, source.rotation, source.scale});
  if (!created)
    return created.error();
  const entity_id entity{scene_object_entity(owner, *created)};
  auto status = error::success;
  if (source.camera) {
    status = owner.entity_configure_camera(entity,
                                           {source.camera->vertical_field_of_view_radians,
                                            source.camera->near_plane, source.camera->far_plane});
    if (status.ok() && source.camera->is_primary)
      status = owner.set_active_camera(entity);
  }
  if (status.ok() && source.mesh_renderer)
    status = owner.entity_set_mesh_renderer(entity, renderer);
  if (status.failed()) {
    (void)owner.object_destroy(*created);
    return status;
  }
  return *created;
}
} // namespace gneiss::engine
