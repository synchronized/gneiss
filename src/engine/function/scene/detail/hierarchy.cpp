// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/detail/hierarchy.hpp"
#include "engine/core/error.hpp"
#include "engine/function/scene/detail/world_state.hpp"
#include "engine/function/scene/scene_types.hpp"

#include "engine/core/math/transform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <utility>

namespace gneiss::engine {
namespace {

math::transform combine(const math::transform& parent, const math::transform& local) noexcept {
  return compose_transform(parent, local);
}
bool is_valid(const math::transform& value) noexcept { return is_valid_transform(value); }

} // namespace

world_hierarchy::node* world_hierarchy::find(entity_id id) noexcept {
  return owner_.get<node>(id.get());
}
const world_hierarchy::node* world_hierarchy::find(entity_id id) const noexcept {
  return std::as_const(owner_).get<node>(id.get());
}

error world_hierarchy::set_identity(entity_id id, scene_node_identity identity) {
  auto* target = find(id);
  if (!target)
    return error::invalid_handle;
  target->identity = std::move(identity);
  return error::success;
}
result<scene_node_identity_view> world_hierarchy::identity(entity_id id) const {
  const auto* target = find(id);
  if (!target)
    return error::invalid_handle;
  return scene_node_identity_view{target->identity.uuid, target->identity.name};
}
const std::vector<entity_id>* world_hierarchy::children(entity_id id) const noexcept {
  const auto* target = find(id);
  return target ? &target->children : nullptr;
}

result<entity_id> world_hierarchy::create(entity_id parent, std::uint64_t entity) {
  if (parent && !find(parent))
    return error::invalid_handle;
  if (entity && !owner_.is_alive(entity))
    return error::invalid_handle;
  if (entity && find(entity_id{entity}))
    return error::invalid_state;
  const bool create_entity = entity == 0;
  try {
    if (parent) {
      auto& children = find(parent)->children;
      children.reserve(children.size() + 1);
    }
    if (create_entity)
      entity = owner_.create_entity();
    owner_.emplace<node>(entity).parent = parent;
    if (parent && owner_.scene_of(parent))
      owner_.assign_scene(entity_id{entity}, owner_.scene_of(parent));
    const entity_id id{entity};
    // 插入组件可能移动存储，重新查询父节点。
    if (parent)
      find(parent)->children.push_back(id);
    return id;
  } catch (...) {
    // 局部状态清理后传播意外异常，不作为普通操作失败继续执行。
    if (create_entity && entity)
      (void)owner_.destroy_entity(entity);
    throw;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达父子关系。
void world_hierarchy::remove_child(entity_id parent, entity_id child) noexcept {
  if (auto* parent_slot = find(parent); parent_slot != nullptr) {
    std::erase(parent_slot->children, child);
  }
}

void world_hierarchy::detach(entity_id id) noexcept {
  if (const auto* target = find(id))
    remove_child(target->parent, id);
}

error world_hierarchy::destroy(entity_id id) {
  if (!find(id))
    return error::invalid_handle;
  return owner_.destroy_entity(id.get()) ? error::success : error::invalid_handle;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达祖先与候选节点。
bool world_hierarchy::is_descendant(entity_id ancestor, entity_id candidate) const noexcept {
  auto current = candidate;
  while (current != null_entity_id) {
    if (current == ancestor) {
      return true;
    }
    const auto* current_slot = find(current);
    if (current_slot == nullptr) {
      return false;
    }
    current = current_slot->parent;
  }
  return false;
}

engine::error world_hierarchy::reparent(entity_id node_id, entity_id parent) {
  auto* target = find(node_id);
  if (target == nullptr || (parent != null_entity_id && find(parent) == nullptr)) {
    return engine::error::invalid_handle;
  }
  if (parent && owner_.scene_of(node_id) != owner_.scene_of(parent))
    return error::invalid_argument;
  if (node_id == parent || is_descendant(node_id, parent)) {
    return engine::error::invalid_argument;
  }
  if (target->parent == parent)
    return error::success;
  {
    if (parent != null_entity_id) {
      find(parent)->children.push_back(node_id);
    }
    remove_child(target->parent, node_id);
    target->parent = parent;
    return engine::error::success;
  }
}

engine::error world_hierarchy::set_local(entity_id node_id,
                                         const math::transform& transform) noexcept {
  auto* target = find(node_id);
  if (target == nullptr) {
    return engine::error::invalid_handle;
  }
  if (!is_valid(transform)) {
    return engine::error::invalid_argument;
  }
  target->local = transform;
  return engine::error::success;
}

const math::transform* world_hierarchy::get_local(entity_id node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? nullptr : &target->local;
}

result<math::transform> world_hierarchy::get_world(entity_id node_id) const noexcept {
  const auto* target = find(node_id);
  if (target == nullptr) {
    return engine::error::invalid_handle;
  }
  auto result = target->local;
  auto parent = target->parent;
  while (parent != null_entity_id) {
    const auto* parent_slot = find(parent);
    result = combine(parent_slot->local, result);
    parent = parent_slot->parent;
  }
  return result;
}

result<math::transform> world_hierarchy::get_world_for_entity(std::uint64_t entity) const noexcept {
  if (!entity)
    return error::invalid_argument;
  return find(entity_id{entity}) ? get_world(entity_id{entity})
                                 : result<math::transform>{error::not_ready};
}

const math::transform* world_hierarchy::get_local_for_entity(std::uint64_t entity) const noexcept {
  return get_local(entity_id{entity});
}

error world_hierarchy::set_local_for_entity(std::uint64_t entity,
                                            const math::transform& transform) noexcept {
  if (!entity)
    return error::invalid_argument;
  return find(entity_id{entity}) ? set_local(entity_id{entity}, transform) : error::not_found;
}

std::uint64_t world_hierarchy::get_entity(entity_id id) const noexcept {
  return find(id) ? id.get() : 0;
}

entity_id world_hierarchy::get_parent(entity_id node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? null_entity_id : target->parent;
}

} // namespace gneiss::engine
