// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "function/scene/world_state.hpp"
#include <algorithm>
#include <gneiss/engine/core/base/assert.hpp>
namespace gneiss {
world_state::~world_state() noexcept {
  try {
    GNEISS_ASSERT(!updating_, "系统执行中不能销毁 World");
    destroying_ = true;
    systems_.clear(); // 先释放系统捕获，再销毁场景和 ECS 组件。
    scene_slots.for_each(resource_type::scene_instance,
                         [&](scene_instance_id id) { destroy_scene_members(id); });
  } catch (...) {
    fatal_current_exception();
  }
}

result<entity_id> world_state::create_object(entity_id parent, std::uint64_t entity) {
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

void world_state::detach(entity_id id) noexcept {
  const auto* object = get<detail::object_state>(id.get());
  if (!object)
    return;
  if (auto* parent = get<detail::object_state>(object->parent.get()))
    std::erase(parent->children, id);
}
}
