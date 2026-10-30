// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/resource/asset_types.hpp"
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
/** 从 World 捕获持久化字段，与 configure_scene_object 支持的组件对称。
 * 不保存派生矩阵或后端句柄；Prefab 根只捕获身份、层级与变换。 */
inline result<object_description> capture_scene_object(world& owner, scene_node_id node,
                                                       bool components = true) {
  object_description output;
  auto identity = owner.node_identity(node);
  if (!identity)
    return identity.error();
  output.uuid = identity->uuid;
  output.name = identity->name;
  scene_node_id parent;
  auto status = owner.node_get_parent(node, parent);
  if (status.failed())
    return status;
  if (parent) {
    auto identity = owner.node_identity(parent);
    if (!identity)
      return identity.error();
    output.parent_uuid = identity->uuid;
  }
  status = owner.node_get_local_transform(node, output.transform);
  if (status.failed())
    return status;
  if (!components)
    return output;
  auto renderer = owner.node_mesh_renderer(node);
  if (renderer) {
    // Scene 接受的资源必须可持久化；缺失身份不能静默写成空 URI。
    const auto mesh_uri = asset_uri(renderer->mesh);
    const auto material_uri = asset_uri(renderer->material);
    if (mesh_uri.empty() || material_uri.empty())
      return error::invalid_state;
    output.components.set(
        mesh_renderer_description{std::string(mesh_uri), std::string(material_uri)});
  } else if (renderer.error() != error::not_found) {
    return renderer.error();
  }
  const entity_id entity{scene_object_entity(owner, node)};
  math::camera_settings camera;
  status = owner.entity_get_camera(entity, camera);
  if (status.ok()) {
    entity_id active;
    auto active_status = owner.get_active_camera(active);
    if (active_status.failed() && active_status != error::not_ready)
      return active_status;
    output.components.set(camera_description{camera, active_status.ok() && active == entity});
  } else if (status != error::not_found) {
    return status;
  }
  return output;
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
        entity, source.components.template get<camera_description>()->settings);
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
