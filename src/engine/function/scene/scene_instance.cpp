// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/function/scene/scene_types.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/function/scene/detail/world_state.hpp"
#include "engine/function/scene/world.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace gneiss::engine {
error world::validate_scene(scene_instance_id id) const noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  return state_->scene_slots.get(id, resource_type::scene_instance) ? error::success
                                                                    : error::invalid_handle;
}
error world::unload_scene(scene_instance_id id) noexcept try {
  auto status = validate_scene(id);
  if (status.failed())
    return status;
  state_->destroy_scene_members(id);
  return state_->scene_slots.destroy(id, resource_type::scene_instance);
} catch (...) {
  fatal_current_exception();
}

result<scene_instance> scene_instance::borrow(world& owner, scene_instance_id id) noexcept {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  auto count = owner.scene_size(id);
  if (!count)
    return count.error();
  scene_instance result;
  result.owner_ = &owner;
  result.id_ = id;
  return result;
}
bool scene_instance::is_valid() const noexcept { return size().has_value(); }
result<std::uint64_t> scene_instance::size() const noexcept {
  if (!owner_)
    return error::invalid_handle;
  return owner_->scene_size(id_);
}
error scene_instance::visit_objects(const std::function<error(game_object)>& visitor,
                                    bool roots_only) const {
  if (!owner_)
    return error::invalid_handle;
  auto status = owner_->validate_scene(id_);
  if (status.failed())
    return status;
  if (!visitor)
    return error::invalid_argument;
  owner_->state_->each_scene_member(id_, [&](entity_id node) {
    if (status.failed() || (roots_only && owner_->state_->scene().get_parent(node)))
      return;
    auto object = owner_->object(entity_id{node.get()});
    if (!object) {
      status = object.error();
      return;
    }
    status = visitor(*object);
  });
  return status;
}
error world::transfer_subtree(game_object root, scene_instance destination, game_object parent) {
  if (!is_owner_thread())
    return error::invalid_state;
  if (root.owner() != this || destination.owner() != this ||
      (parent.owner() && parent.owner() != this))
    return error::invalid_handle;
  auto status = validate_scene(destination.id());
  if (status.failed())
    return status;
  auto source = root.scene();
  if (!source)
    return source.error();
  if (parent.owner()) {
    auto member = destination.contains(parent);
    if (!member)
      return member.error();
    if (!*member)
      return error::invalid_argument;
  }
  std::vector<entity_id> nodes{entity_id{root.entity().get()}};
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (parent.owner() && nodes[i].get() == parent.entity().get())
      return error::invalid_argument;
    if (state_->scene_of(nodes[i]) != source->id())
      return error::invalid_state;
    auto children = state_->scene().children(nodes[i]);
    if (!children)
      return error::invalid_handle;
    nodes.insert(nodes.end(), children->begin(), children->end());
  }
  // 已有归属组件原位更新，不在提交阶段分配组件存储。
  auto assign = [&](scene_instance_id id) {
    for (auto node : nodes)
      state_->get<scene_membership>(node.get())->scene = id;
  };
  assign(destination.id());
  try {
    status = root.reparent(parent);
  } catch (...) {
    assign(source->id());
    throw;
  }
  if (status.failed())
    assign(source->id());
  return status;
}
result<bool> scene_instance::contains(game_object object) const noexcept {
  if (!owner_)
    return error::invalid_handle;
  auto status = owner_->validate_scene(id_);
  if (status.failed())
    return status;
  if (object.owner() != owner_)
    return false;
  return owner_->state_->is_alive(object.entity().get()) &&
         owner_->state_->scene_of(object.entity()) == id_;
}

error scene_instance::unload() const noexcept {
  if (!owner_)
    return error::invalid_handle;
  return owner_->unload_scene(id_);
}

result<std::uint64_t> world::scene_size(scene_instance_id instance) const {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  std::uint64_t count{};
  state_->each_scene_member(instance, [&](entity_id) { ++count; });
  return count;
}

result<game_object> scene_instance::create(scene_node_identity identity,
                                           const math::transform& transform,
                                           game_object parent) const {
  if (!owner_)
    return error::invalid_handle;
  auto status = owner_->validate_scene(id_);
  if (status.failed())
    return status;
  if (parent.owner()) {
    auto member = contains(parent);
    if (!member)
      return member.error();
    if (!*member)
      return error::invalid_handle;
  }
  if (identity.uuid) {
    bool duplicate = false;
    owner_->state_->each_scene_member(id_, [&](entity_id entity) {
      if (owner_->state_->scene().identity(entity)->uuid == identity.uuid)
        duplicate = true;
    });
    if (duplicate)
      return error::invalid_argument;
  }
  auto& state = *owner_->state_;
  auto created = state.scene().create(parent.entity(), 0);
  if (!created)
    return created.error();
  try {
    status = state.scene().set_local(*created, transform);
    if (status.ok())
      status = state.scene().set_identity(*created, std::move(identity));
    if (status.failed()) {
      (void)state.destroy_entity(created->get());
      return status;
    }
    state.assign_scene(*created, id_);
  } catch (...) {
    (void)state.destroy_entity(created->get());
    throw;
  }
  return owner_->find(*created);
}
}
