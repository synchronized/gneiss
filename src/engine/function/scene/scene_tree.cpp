// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_tree.hpp"
#include "engine/core/error.hpp"
#include "engine/core/scene_types.hpp"

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

scene_node_id scene_tree::encode(std::uint16_t index, std::uint16_t generation) const noexcept {
  return scene_node_id{(static_cast<std::uint64_t>(domain_) << 32U) |
                       (static_cast<std::uint64_t>(generation) << 16U) |
                       static_cast<std::uint64_t>(index + 1U)};
}

scene_tree::slot* scene_tree::find(scene_node_id id) noexcept {
  return const_cast<slot*>(std::as_const(*this).find(id));
}

const scene_tree::slot* scene_tree::find(scene_node_id id) const noexcept {
  if (id == null_scene_node_id || static_cast<std::uint32_t>(id.get() >> 32U) != domain_) {
    return nullptr;
  }
  const auto encoded_index = static_cast<std::uint16_t>(id.get());
  if (encoded_index == 0U) {
    return nullptr;
  }
  const auto index = static_cast<std::size_t>(encoded_index - 1U);
  const auto generation = static_cast<std::uint16_t>(id.get() >> 16U);
  if (index >= slots_.size() || !slots_[index].value || slots_[index].generation != generation) {
    return nullptr;
  }
  return &slots_[index];
}

error scene_tree::set_identity(scene_node_id id, scene_node_identity identity) noexcept {
  auto* target = find(id);
  if (!target)
    return error::invalid_handle;
  target->value->identity = std::move(identity);
  return error::success;
}
result<scene_node_identity_view> scene_tree::identity(scene_node_id id) const noexcept {
  const auto* target = find(id);
  if (!target)
    return error::invalid_handle;
  return scene_node_identity_view{target->value->identity.uuid, target->value->identity.name};
}
const std::vector<scene_node_id>* scene_tree::children(scene_node_id id) const noexcept {
  const auto* target = find(id);
  return target ? &target->value->children : nullptr;
}

result<scene_node_id> scene_tree::create(scene_node_id parent, std::uint64_t entity) noexcept {
  if (parent != null_scene_node_id) {
    const auto* value = find(parent);
    if (value == nullptr) {
      return engine::error::invalid_handle;
    }
  }
  if (entity != UINT64_C(0) && std::ranges::any_of(slots_, [entity](const slot& candidate) {
        return candidate.value && candidate.value->entity == entity;
      })) {
    return engine::error::invalid_state;
  }
  try {
    if (parent != null_scene_node_id) {
      auto& children = find(parent)->value->children;
      children.reserve(children.size() + 1U);
    }
    std::size_t index = 0;
    while (index < slots_.size() && (slots_[index].value || slots_[index].retired)) {
      ++index;
    }
    if (index >= std::numeric_limits<std::uint16_t>::max()) {
      return engine::error::out_of_memory;
    }
    if (index == slots_.size()) {
      slots_.emplace_back();
    }
    auto& target = slots_[index];
    target.value = std::make_unique<node>();
    target.value->parent = parent;
    target.value->entity = entity;
    const auto id = encode(static_cast<std::uint16_t>(index), target.generation);
    if (parent != null_scene_node_id) {
      find(parent)->value->children.push_back(id);
    }
    return id;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达父子关系。
void scene_tree::remove_child(scene_node_id parent, scene_node_id child) noexcept {
  if (auto* parent_slot = find(parent); parent_slot != nullptr) {
    std::erase(parent_slot->value->children, child);
  }
}

void scene_tree::destroy_subtree(scene_node_id id) noexcept {
  auto* target = find(id);
  const auto children = std::move(target->value->children);
  for (const auto child : children) {
    destroy_subtree(child);
  }
  const auto index = static_cast<std::size_t>(static_cast<std::uint16_t>(id.get()) - 1U);
  auto& target_slot = slots_[index];
  target_slot.value.reset();
  if (target_slot.generation == std::numeric_limits<std::uint16_t>::max()) {
    target_slot.retired = true;
  } else {
    ++target_slot.generation;
  }
}

engine::error scene_tree::destroy(scene_node_id node_id) noexcept {
  auto* target = find(node_id);
  if (target == nullptr) {
    return engine::error::invalid_handle;
  }

  remove_child(target->value->parent, node_id);
  destroy_subtree(node_id);
  return engine::error::success;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达祖先与候选节点。
bool scene_tree::is_descendant(scene_node_id ancestor, scene_node_id candidate) const noexcept {
  auto current = candidate;
  while (current != null_scene_node_id) {
    if (current == ancestor) {
      return true;
    }
    const auto* current_slot = find(current);
    if (current_slot == nullptr) {
      return false;
    }
    current = current_slot->value->parent;
  }
  return false;
}

engine::error scene_tree::reparent(scene_node_id node_id, scene_node_id parent) noexcept {
  auto* target = find(node_id);
  if (target == nullptr || (parent != null_scene_node_id && find(parent) == nullptr)) {
    return engine::error::invalid_handle;
  }
  if (node_id == parent || is_descendant(node_id, parent)) {
    return engine::error::invalid_argument;
  }
  try {
    if (parent != null_scene_node_id) {
      find(parent)->value->children.push_back(node_id);
    }
    remove_child(target->value->parent, node_id);
    target->value->parent = parent;
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error scene_tree::set_local(scene_node_id node_id,
                                    const math::transform& transform) noexcept {
  auto* target = find(node_id);
  if (target == nullptr) {
    return engine::error::invalid_handle;
  }
  if (!is_valid(transform)) {
    return engine::error::invalid_argument;
  }
  target->value->local = transform;
  return engine::error::success;
}

const math::transform* scene_tree::get_local(scene_node_id node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? nullptr : &target->value->local;
}

result<math::transform> scene_tree::get_world(scene_node_id node_id) const noexcept {
  const auto* target = find(node_id);
  if (target == nullptr) {
    return engine::error::invalid_handle;
  }
  auto result = target->value->local;
  auto parent = target->value->parent;
  while (parent != null_scene_node_id) {
    const auto* parent_slot = find(parent);
    result = combine(parent_slot->value->local, result);
    parent = parent_slot->value->parent;
  }
  return result;
}

result<math::transform> scene_tree::get_world_for_entity(std::uint64_t entity) const noexcept {
  if (entity == UINT64_C(0)) {
    return engine::error::invalid_argument;
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    const auto& candidate = slots_[index];
    if (candidate.value && candidate.value->entity == entity) {
      return get_world(encode(static_cast<std::uint16_t>(index), candidate.generation));
    }
  }
  return engine::error::not_ready;
}

result<scene_node_id> scene_tree::node_for_entity(std::uint64_t entity) const noexcept {
  for (std::size_t i = 0; i < slots_.size(); ++i)
    if (slots_[i].value && slots_[i].value->entity == entity)
      return encode(static_cast<std::uint16_t>(i), slots_[i].generation);
  return error::not_found;
}
const math::transform* scene_tree::get_local_for_entity(std::uint64_t entity) const noexcept {
  if (entity == UINT64_C(0)) {
    return nullptr;
  }
  const auto found = std::ranges::find_if(slots_, [entity](const slot& candidate) {
    return candidate.value && candidate.value->entity == entity;
  });
  return found == slots_.end() ? nullptr : &found->value->local;
}

engine::error scene_tree::set_local_for_entity(std::uint64_t entity,
                                               const math::transform& transform) noexcept {
  if (entity == UINT64_C(0)) {
    return engine::error::invalid_argument;
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    const auto& candidate = slots_[index];
    if (candidate.value && candidate.value->entity == entity) {
      return set_local(encode(static_cast<std::uint16_t>(index), candidate.generation), transform);
    }
  }
  return engine::error::not_found;
}

std::uint64_t scene_tree::get_entity(scene_node_id node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? UINT64_C(0) : target->value->entity;
}

scene_node_id scene_tree::get_parent(scene_node_id node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? null_scene_node_id : target->value->parent;
}

void scene_tree::detach_entity(std::uint64_t entity) noexcept {
  for (auto& target : slots_) {
    if (target.value && target.value->entity == entity) {
      target.value->entity = UINT64_C(0);
    }
  }
}

} // namespace gneiss::engine
