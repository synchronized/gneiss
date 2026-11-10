// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/function/scene/scene_types.hpp"

#include "engine/core/identifier_conversion.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_state.hpp"

#include "engine/function/scene/scene_object.hpp"

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
  owner_->state_->each_scene_member(id_, [&](scene_node_id node) {
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
  std::vector<scene_node_id> nodes{scene_node_id{root.entity().get()}};
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
  return owner_->validate_scene_node(id_, scene_node_id{object.entity().get()}).ok();
}
result<game_object> scene_instance::create(scene_node_identity identity,
                                           const math::transform& transform,
                                           game_object parent) const {
  if (!owner_)
    return error::invalid_handle;
  if (parent.owner() && parent.owner() != owner_)
    return error::invalid_handle;
  auto created = owner_->create_scene_node(id_, {.uuid = identity.uuid,
                                                 .name = identity.name,
                                                 .parent = scene_node_id{parent.entity().get()},
                                                 .local_transform = transform});
  if (!created)
    return created.error();
  return owner_->object(entity_id{created->get()});
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
  state_->each_scene_member(instance, [&](scene_node_id) { ++count; });
  return count;
}
error world::validate_scene_node(scene_instance_id instance, scene_node_id node) const noexcept {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  return state_->scene_of(node) == instance ? error::success : error::invalid_handle;
}
result<scene_node_id> world::find_scene_node(scene_instance_id instance, guid uuid) const {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  scene_node_id found;
  state_->each_scene_member(instance, [&](scene_node_id node) {
    if (state_->scene().identity(node)->uuid == uuid)
      found = node;
  });
  if (!found)
    return error::not_found;
  return found;
}

result<scene_node_id> world::create_scene_node(scene_instance_id instance,
                                               const node_creation& desc) {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  if (desc.uuid && find_scene_node(instance, desc.uuid))
    return error::invalid_argument;
  if (desc.parent && validate_scene_node(instance, desc.parent).failed())
    return error::invalid_handle;
  auto created = create_object({desc.uuid, std::string(desc.name)}, desc.local_transform,
                               find(entity_id{desc.parent.get()}));
  if (!created)
    return created.error();
  const auto node = scene_node_id{created->entity().get()};
  try {
    state_->assign_scene(node, instance);
  } catch (...) {
    (void)created->destroy();
    throw;
  }
  return node;
}

} // namespace gneiss::engine
