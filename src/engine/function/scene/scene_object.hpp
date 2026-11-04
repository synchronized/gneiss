// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/component_adapter.hpp"
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
    auto parent_identity = owner.node_identity(parent);
    if (!parent_identity)
      return parent_identity.error();
    output.parent_uuid = parent_identity->uuid;
  }
  status = owner.node_get_local_transform(node, output.transform);
  if (status.failed())
    return status;
  if (!components)
    return output;
  auto captured = capture_components(owner, node);
  if (!captured)
    return captured.error();
  output.components = std::move(*captured);
  return output;
}
/** 未注册适配器的描述不得被静默忽略。 */
inline error validate_scene_components(const object_description& source) noexcept {
  if (source.prefab && !source.components.empty())
    return error::invalid_argument;
  return validate_components(source.components);
}
/** 将已解析组件安装到节点；创建失败的回收由调用者负责。 */
inline error configure_scene_object(world& owner, scene_node_id node,
                                    const object_description& source, const mesh_renderer& renderer,
                                    resource_service* resources = nullptr) {
  const auto validation = validate_scene_components(source);
  if (validation.failed())
    return validation;
  return install_components({owner, node, renderer, resources}, source.components);
}
/** 普通对象与 Prefab 共用实例化路径，输入资源仅在组件内长期持有。 */
inline result<scene_node_id> create_scene_object(world& owner, const object_description& source,
                                                 scene_node_id parent,
                                                 const mesh_renderer& renderer,
                                                 resource_service* resources = nullptr) {
  auto created = owner.object_create(parent, {source.uuid, source.name}, source.transform);
  if (!created)
    return created.error();
  const auto status = configure_scene_object(owner, *created, source, renderer, resources);
  if (status.failed()) {
    (void)owner.object_destroy(*created);
    return status;
  }
  return *created;
}
} // namespace gneiss::engine
