// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_tree.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"

#include "engine/core/math/functions.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <utility>

namespace gneiss::engine {
namespace {

gneiss::engine::transform combine(const gneiss::engine::transform& parent,
                                  const gneiss::engine::transform& local) noexcept {
  return compose_transform(parent, local);
}
bool is_valid(const gneiss::engine::transform& value) noexcept { return valid_transform(value); }

} // namespace

gneiss::engine::rid scene_tree::encode(std::uint16_t index,
                                       std::uint16_t generation) const noexcept {
  return (static_cast<std::uint64_t>(domain_) << 32U) |
         (static_cast<std::uint64_t>(generation) << 16U) | static_cast<std::uint64_t>(index + 1U);
}

scene_tree::slot* scene_tree::find(gneiss::engine::rid id) noexcept {
  return const_cast<slot*>(std::as_const(*this).find(id));
}

gneiss::engine::result
scene_tree::bind_publication(gneiss::engine::rid id,
                             const gneiss::engine::publication& group) noexcept {
  auto* node = find(id);
  if (!node || node->value->publication || !group || group->domain_ != domain_ ||
      !group->published_ || group->retired_) {
    return gneiss::engine::result::invalid_state;
  }
  node->value->publication = group;
  return gneiss::engine::result::success;
}

bool scene_tree::publication_closed(const gneiss::engine::publication& group) const noexcept {
  for (const auto& slot : slots_) {
    if (!slot.value || slot.value->publication != group) {
      continue;
    }
    for (const auto child : slot.value->children) {
      const auto* value = find_raw(child);
      if (!value || value->value->publication != group) {
        return false;
      }
    }
  }
  return true;
}

const scene_tree::slot* scene_tree::find(gneiss::engine::rid id) const noexcept {
  const auto* value = find_raw(id);
  return value != nullptr && (!value->value->publication || value->value->publication->published_)
             ? value
             : nullptr;
}

scene_tree::slot* scene_tree::find_raw(gneiss::engine::rid id) noexcept {
  return const_cast<slot*>(std::as_const(*this).find_raw(id));
}

const scene_tree::slot* scene_tree::find_raw(gneiss::engine::rid id) const noexcept {
  if (id == gneiss::engine::null_rid || static_cast<std::uint32_t>(id >> 32U) != domain_) {
    return nullptr;
  }
  const auto encoded_index = static_cast<std::uint16_t>(id);
  if (encoded_index == 0U) {
    return nullptr;
  }
  const auto index = static_cast<std::size_t>(encoded_index - 1U);
  const auto generation = static_cast<std::uint16_t>(id >> 16U);
  if (index >= slots_.size() || !slots_[index].value || slots_[index].generation != generation) {
    return nullptr;
  }
  return &slots_[index];
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 两种标识在 C ABI 中均为定宽整数。
gneiss::engine::result scene_tree::create(gneiss::engine::rid parent, std::uint64_t entity,
                                          gneiss::engine::rid* out_node) noexcept {
  return create_impl({}, parent, entity, out_node);
}

gneiss::engine::result scene_tree::create_staged(const gneiss::engine::publication& group,
                                                 gneiss::engine::rid parent, std::uint64_t entity,
                                                 gneiss::engine::rid* output) noexcept {
  if (output != nullptr) {
    *output = gneiss::engine::null_rid;
  }
  if (!group || group->domain_ != domain_ || group->published_ || group->retired_) {
    return gneiss::engine::result::invalid_state;
  }
  return create_impl(group, parent, entity, output);
}

gneiss::engine::result scene_tree::create_impl(const gneiss::engine::publication& group,
                                               gneiss::engine::rid parent, std::uint64_t entity,
                                               gneiss::engine::rid* out_node) noexcept {
  if (out_node == nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  *out_node = gneiss::engine::null_rid;
  if (parent != gneiss::engine::null_rid) {
    const auto* value = find_raw(parent);
    if (value == nullptr || (value->value->publication && !value->value->publication->published_ &&
                             value->value->publication != group)) {
      return gneiss::engine::result::invalid_handle;
    }
  }
  if (entity != UINT64_C(0) && std::ranges::any_of(slots_, [entity](const slot& candidate) {
        return candidate.value && candidate.value->entity == entity;
      })) {
    return gneiss::engine::result::invalid_state;
  }
  *out_node = gneiss::engine::null_rid;
  try {
    if (parent != gneiss::engine::null_rid) {
      auto& children = find_raw(parent)->value->children;
      children.reserve(children.size() + 1U);
    }
    std::size_t index = 0;
    while (index < slots_.size() && (slots_[index].value || slots_[index].retired)) {
      ++index;
    }
    if (index >= std::numeric_limits<std::uint16_t>::max()) {
      return gneiss::engine::result::out_of_memory;
    }
    if (index == slots_.size()) {
      slots_.emplace_back();
    }
    auto& target = slots_[index];
    target.value = std::make_unique<node>();
    target.value->parent = parent;
    target.value->entity = entity;
    target.value->publication = group;
    const auto id = encode(static_cast<std::uint16_t>(index), target.generation);
    if (parent != gneiss::engine::null_rid) {
      find_raw(parent)->value->children.push_back(id);
    }
    *out_node = id;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达父子关系。
void scene_tree::remove_child(gneiss::engine::rid parent, gneiss::engine::rid child) noexcept {
  if (auto* parent_slot = find_raw(parent); parent_slot != nullptr) {
    std::erase(parent_slot->value->children, child);
  }
}

void scene_tree::destroy_subtree(gneiss::engine::rid id) noexcept {
  auto* target = find_raw(id);
  const auto children = std::move(target->value->children);
  for (const auto child : children) {
    destroy_subtree(child);
  }
  const auto index = static_cast<std::size_t>(static_cast<std::uint16_t>(id) - 1U);
  auto& target_slot = slots_[index];
  target_slot.value.reset();
  if (target_slot.generation == std::numeric_limits<std::uint16_t>::max()) {
    target_slot.retired = true;
  } else {
    ++target_slot.generation;
  }
}

gneiss::engine::result scene_tree::destroy(gneiss::engine::rid node_id) noexcept {
  auto* target = find(node_id);
  if (target == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  if (has_unpublished_descendant(node_id)) {
    return gneiss::engine::result::not_ready;
  }
  remove_child(target->value->parent, node_id);
  destroy_subtree(node_id);
  return gneiss::engine::result::success;
}

bool scene_tree::has_unpublished_descendant(gneiss::engine::rid id) const noexcept {
  const auto* value = find_raw(id);
  if (value->value->publication && !value->value->publication->published_) {
    return true;
  }
  return std::ranges::any_of(value->value->children,
                             [this](auto child) { return has_unpublished_descendant(child); });
}

std::uint64_t scene_tree::get_staged_entity(const gneiss::engine::publication& group,
                                            gneiss::engine::rid id) const noexcept {
  const auto* value = find_raw(id);
  return group && group->domain_ == domain_ && !group->published_ && value != nullptr &&
                 value->value->publication == group
             ? value->value->entity
             : UINT64_C(0);
}

gneiss::engine::result
scene_tree::set_staged_local(const gneiss::engine::publication& group, gneiss::engine::rid id,
                             const gneiss::engine::transform& transform) noexcept {
  auto* value = find_raw(id);
  if (!group || group->domain_ != domain_ || group->published_ || group->retired_ ||
      value == nullptr || value->value->publication != group) {
    return gneiss::engine::result::invalid_handle;
  }
  if (!is_valid(transform)) {
    return gneiss::engine::result::invalid_argument;
  }
  value->value->local = transform;
  return gneiss::engine::result::success;
}

gneiss::engine::result scene_tree::destroy_staged_leaf(const gneiss::engine::publication& group,
                                                       gneiss::engine::rid id) noexcept {
  auto* value = find_raw(id);
  if (!group || group->domain_ != domain_ || group->published_ || value == nullptr ||
      value->value->publication != group) {
    return gneiss::engine::result::invalid_handle;
  }
  if (!value->value->children.empty()) {
    return gneiss::engine::result::not_ready;
  }
  remove_child(value->value->parent, id);
  destroy_subtree(id);
  return gneiss::engine::result::success;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达祖先与候选节点。
bool scene_tree::is_descendant(gneiss::engine::rid ancestor,
                               gneiss::engine::rid candidate) const noexcept {
  auto current = candidate;
  while (current != gneiss::engine::null_rid) {
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

gneiss::engine::result scene_tree::reparent(gneiss::engine::rid node_id,
                                            gneiss::engine::rid parent) noexcept {
  auto* target = find(node_id);
  if (target == nullptr || (parent != gneiss::engine::null_rid && find(parent) == nullptr)) {
    return gneiss::engine::result::invalid_handle;
  }
  if (node_id == parent || is_descendant(node_id, parent)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    if (parent != gneiss::engine::null_rid) {
      find(parent)->value->children.push_back(node_id);
    }
    remove_child(target->value->parent, node_id);
    target->value->parent = parent;
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result scene_tree::set_local(gneiss::engine::rid node_id,
                                             const gneiss::engine::transform& transform) noexcept {
  auto* target = find(node_id);
  if (target == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  if (!is_valid(transform)) {
    return gneiss::engine::result::invalid_argument;
  }
  target->value->local = transform;
  return gneiss::engine::result::success;
}

const gneiss::engine::transform* scene_tree::get_local(gneiss::engine::rid node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? nullptr : &target->value->local;
}

gneiss::engine::result
scene_tree::get_world(gneiss::engine::rid node_id,
                      gneiss::engine::transform* out_transform) const noexcept {
  const auto* target = find(node_id);
  if (target == nullptr || out_transform == nullptr) {
    return gneiss::engine::result::invalid_handle;
  }
  auto result = target->value->local;
  auto parent = target->value->parent;
  while (parent != gneiss::engine::null_rid) {
    const auto* parent_slot = find(parent);
    result = combine(parent_slot->value->local, result);
    parent = parent_slot->value->parent;
  }
  *out_transform = result;
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_tree::get_world_for_entity(std::uint64_t entity,
                                 gneiss::engine::transform* out_transform) const noexcept {
  if (entity == UINT64_C(0) || out_transform == nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    const auto& candidate = slots_[index];
    if (candidate.value && candidate.value->entity == entity) {
      return get_world(encode(static_cast<std::uint16_t>(index), candidate.generation),
                       out_transform);
    }
  }
  return gneiss::engine::result::not_ready;
}

const gneiss::engine::transform*
scene_tree::get_local_for_entity(std::uint64_t entity) const noexcept {
  if (entity == UINT64_C(0)) {
    return nullptr;
  }
  const auto found = std::ranges::find_if(slots_, [entity](const slot& candidate) {
    return candidate.value && candidate.value->entity == entity;
  });
  return found == slots_.end() ||
                 (found->value->publication && !found->value->publication->published_)
             ? nullptr
             : &found->value->local;
}

gneiss::engine::result
scene_tree::set_local_for_entity(std::uint64_t entity,
                                 const gneiss::engine::transform& transform) noexcept {
  if (entity == UINT64_C(0)) {
    return gneiss::engine::result::invalid_argument;
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    const auto& candidate = slots_[index];
    if (candidate.value && candidate.value->entity == entity) {
      return set_local(encode(static_cast<std::uint16_t>(index), candidate.generation), transform);
    }
  }
  return gneiss::engine::result::not_found;
}

std::uint64_t scene_tree::get_entity(gneiss::engine::rid node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? UINT64_C(0) : target->value->entity;
}

gneiss::engine::rid scene_tree::get_parent(gneiss::engine::rid node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? gneiss::engine::null_rid : target->value->parent;
}

void scene_tree::detach_entity(std::uint64_t entity) noexcept {
  for (auto& target : slots_) {
    if (target.value && target.value->entity == entity) {
      target.value->entity = UINT64_C(0);
    }
  }
}

} // namespace gneiss::engine
