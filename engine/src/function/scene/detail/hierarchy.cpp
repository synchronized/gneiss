// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/scene/detail/world_impl.hpp"
#include <algorithm>
namespace gneiss {
result<entity_id> world::implementation::create_object(entity_id parent, std::uint64_t entity) {
  if (parent && !get<detail::object_state>(parent.get()))
    return error::invalid_handle;
  if (entity && !is_alive(entity))
    return error::invalid_handle;
  if (entity && get<detail::object_state>(entity))
    return error::invalid_state;
  const auto previous_scene = scene_of(entity_id{entity});
  const bool allocate_entity = entity == 0;
  try {
    if (parent) {
      auto& children = get<detail::object_state>(parent.get())->children;
      children.reserve(children.size() + 1);
    }
    if (allocate_entity)
      entity = create_entity();
    emplace<detail::object_state>(entity).parent = parent;
    // 已有辅助实体转为对象时迁移归属，避免同一实体被枚举两次。
    (void)remove<detail::auxiliary_scene_membership>(entity);
    assign_scene(entity_id{entity}, parent ? scene_of(parent) : previous_scene);
    const entity_id id{entity};
    // 插入组件可能移动存储，重新查询父节点。
    if (parent)
      get<detail::object_state>(parent.get())->children.push_back(id);
    return id;
  } catch (...) {
    // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
    if (allocate_entity && entity)
      (void)destroy_entity(entity);
    throw;
  }
}

void world::implementation::detach(entity_id id) noexcept {
  const auto* object = get<detail::object_state>(id.get());
  if (!object)
    return;
  if (auto* parent = get<detail::object_state>(object->parent.get()))
    std::erase(parent->children, id);
}
}
