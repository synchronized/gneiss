// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/scene_tree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>
#include <utility>

namespace gneiss::scene_internal {
namespace {

gneiss_transform combine(const gneiss_transform& parent, const gneiss_transform& local) noexcept {
  const auto px = parent.rotation[0];
  const auto py = parent.rotation[1];
  const auto pz = parent.rotation[2];
  const auto pw = parent.rotation[3];
  const std::array scaled = {local.translation[0] * parent.scale[0],
                             local.translation[1] * parent.scale[1],
                             local.translation[2] * parent.scale[2]};
  const std::array uv = {(py * scaled[2]) - (pz * scaled[1]), (pz * scaled[0]) - (px * scaled[2]),
                         (px * scaled[1]) - (py * scaled[0])};
  const std::array uuv = {(py * uv[2]) - (pz * uv[1]), (pz * uv[0]) - (px * uv[2]),
                          (px * uv[1]) - (py * uv[0])};

  gneiss_transform result = GNEISS_TRANSFORM_IDENTITY;
  for (std::size_t index = 0; index < 3; ++index) {
    result.translation[index] =
        parent.translation[index] + scaled[index] + (2.0F * ((pw * uv[index]) + uuv[index]));
    result.scale[index] = parent.scale[index] * local.scale[index];
  }
  result.rotation[0] = (pw * local.rotation[0]) + (px * local.rotation[3]) +
                       (py * local.rotation[2]) - (pz * local.rotation[1]);
  result.rotation[1] = (pw * local.rotation[1]) - (px * local.rotation[2]) +
                       (py * local.rotation[3]) + (pz * local.rotation[0]);
  result.rotation[2] = (pw * local.rotation[2]) + (px * local.rotation[1]) -
                       (py * local.rotation[0]) + (pz * local.rotation[3]);
  result.rotation[3] = (pw * local.rotation[3]) - (px * local.rotation[0]) -
                       (py * local.rotation[1]) - (pz * local.rotation[2]);
  return result;
}

bool is_valid(const gneiss_transform& value) noexcept {
  const auto finite = [](float component) { return std::isfinite(component); };
  if (!std::ranges::all_of(value.translation, finite) ||
      !std::ranges::all_of(value.rotation, finite) || !std::ranges::all_of(value.scale, finite)) {
    return false;
  }
  const auto rotation_length = std::sqrt(std::inner_product(
      std::begin(value.rotation), std::end(value.rotation), std::begin(value.rotation), 0.0F));
  constexpr auto tolerance = 1.0e-4F;
  return std::abs(rotation_length - 1.0F) <= tolerance &&
         std::ranges::all_of(value.scale,
                             [](float component) { return std::abs(component) >= 1.0e-6F; });
}

} // namespace

gneiss_scene_node_id scene_tree::encode(std::uint16_t index,
                                        std::uint16_t generation) const noexcept {
  return (static_cast<std::uint64_t>(domain_) << 32U) |
         (static_cast<std::uint64_t>(generation) << 16U) | static_cast<std::uint64_t>(index + 1U);
}

scene_tree::slot* scene_tree::find(gneiss_scene_node_id id) noexcept {
  return const_cast<slot*>(std::as_const(*this).find(id));
}

const scene_tree::slot* scene_tree::find(gneiss_scene_node_id id) const noexcept {
  if (id == GNEISS_NULL_SCENE_NODE_ID || static_cast<std::uint32_t>(id >> 32U) != domain_) {
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
gneiss_result scene_tree::create(gneiss_scene_node_id parent, gneiss_entity_id entity,
                                 gneiss_scene_node_id* out_node) noexcept {
  if (out_node == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (parent != GNEISS_NULL_SCENE_NODE_ID && find(parent) == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  if (entity != GNEISS_NULL_ENTITY_ID &&
      std::ranges::any_of(slots_, [entity](const slot& candidate) {
        return candidate.value && candidate.value->entity == entity;
      })) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  *out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    if (parent != GNEISS_NULL_SCENE_NODE_ID) {
      auto& children = find(parent)->value->children;
      children.reserve(children.size() + 1U);
    }
    std::size_t index = 0;
    while (index < slots_.size() && (slots_[index].value || slots_[index].retired)) {
      ++index;
    }
    if (index >= std::numeric_limits<std::uint16_t>::max()) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    }
    if (index == slots_.size()) {
      slots_.emplace_back();
    }
    auto& target = slots_[index];
    target.value = std::make_unique<node>();
    target.value->parent = parent;
    target.value->entity = entity;
    const auto id = encode(static_cast<std::uint16_t>(index), target.generation);
    if (parent != GNEISS_NULL_SCENE_NODE_ID) {
      find(parent)->value->children.push_back(id);
    }
    *out_node = id;
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达父子关系。
void scene_tree::remove_child(gneiss_scene_node_id parent, gneiss_scene_node_id child) noexcept {
  if (auto* parent_slot = find(parent); parent_slot != nullptr) {
    std::erase(parent_slot->value->children, child);
  }
}

void scene_tree::destroy_subtree(gneiss_scene_node_id id) noexcept {
  auto* target = find(id);
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

gneiss_result scene_tree::destroy(gneiss_scene_node_id node_id) noexcept {
  auto* target = find(node_id);
  if (target == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  remove_child(target->value->parent, node_id);
  destroy_subtree(node_id);
  return GNEISS_SUCCESS;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): 参数名称明确表达祖先与候选节点。
bool scene_tree::is_descendant(gneiss_scene_node_id ancestor,
                               gneiss_scene_node_id candidate) const noexcept {
  auto current = candidate;
  while (current != GNEISS_NULL_SCENE_NODE_ID) {
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

gneiss_result scene_tree::reparent(gneiss_scene_node_id node_id,
                                   gneiss_scene_node_id parent) noexcept {
  auto* target = find(node_id);
  if (target == nullptr || (parent != GNEISS_NULL_SCENE_NODE_ID && find(parent) == nullptr)) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  if (node_id == parent || is_descendant(node_id, parent)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    if (parent != GNEISS_NULL_SCENE_NODE_ID) {
      find(parent)->value->children.push_back(node_id);
    }
    remove_child(target->value->parent, node_id);
    target->value->parent = parent;
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result scene_tree::set_local(gneiss_scene_node_id node_id,
                                    const gneiss_transform& transform) noexcept {
  auto* target = find(node_id);
  if (target == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  if (!is_valid(transform)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  target->value->local = transform;
  return GNEISS_SUCCESS;
}

const gneiss_transform* scene_tree::get_local(gneiss_scene_node_id node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? nullptr : &target->value->local;
}

gneiss_result scene_tree::get_world(gneiss_scene_node_id node_id,
                                    gneiss_transform* out_transform) const noexcept {
  const auto* target = find(node_id);
  if (target == nullptr || out_transform == nullptr) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  auto result = target->value->local;
  auto parent = target->value->parent;
  while (parent != GNEISS_NULL_SCENE_NODE_ID) {
    const auto* parent_slot = find(parent);
    result = combine(parent_slot->value->local, result);
    parent = parent_slot->value->parent;
  }
  *out_transform = result;
  return GNEISS_SUCCESS;
}

gneiss_result scene_tree::get_world_for_entity(gneiss_entity_id entity,
                                               gneiss_transform* out_transform) const noexcept {
  if (entity == GNEISS_NULL_ENTITY_ID || out_transform == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    const auto& candidate = slots_[index];
    if (candidate.value && candidate.value->entity == entity) {
      return get_world(encode(static_cast<std::uint16_t>(index), candidate.generation),
                       out_transform);
    }
  }
  return GNEISS_ERROR_NOT_READY;
}

const gneiss_transform* scene_tree::get_local_for_entity(gneiss_entity_id entity) const noexcept {
  if (entity == GNEISS_NULL_ENTITY_ID) {
    return nullptr;
  }
  const auto found = std::ranges::find_if(slots_, [entity](const slot& candidate) {
    return candidate.value && candidate.value->entity == entity;
  });
  return found == slots_.end() ? nullptr : &found->value->local;
}

gneiss_result scene_tree::set_local_for_entity(gneiss_entity_id entity,
                                               const gneiss_transform& transform) noexcept {
  if (entity == GNEISS_NULL_ENTITY_ID) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    const auto& candidate = slots_[index];
    if (candidate.value && candidate.value->entity == entity) {
      return set_local(encode(static_cast<std::uint16_t>(index), candidate.generation), transform);
    }
  }
  return GNEISS_ERROR_NOT_FOUND;
}

gneiss_entity_id scene_tree::get_entity(gneiss_scene_node_id node_id) const noexcept {
  const auto* target = find(node_id);
  return target == nullptr ? GNEISS_NULL_ENTITY_ID : target->value->entity;
}

void scene_tree::detach_entity(gneiss_entity_id entity) noexcept {
  for (auto& target : slots_) {
    if (target.value && target.value->entity == entity) {
      target.value->entity = GNEISS_NULL_ENTITY_ID;
    }
  }
}

} // namespace gneiss::scene_internal
