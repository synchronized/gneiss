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
result<game_object> scene_instance::find(guid uuid) const noexcept {
  if (!owner_)
    return error::invalid_handle;
  auto node = owner_->find_scene_node(id_, uuid);
  if (!node)
    return node.error();
  return owner_->object(entity_id{node->get()});
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

std::vector<scene_node_id> world::scene_members(scene_instance_id instance) const {
  std::vector<scene_node_id> result;
  state_->each_scene_member(instance, [&](scene_node_id node) { result.push_back(node); });
  std::ranges::sort(result, {}, [](scene_node_id node) { return node.get(); });
  return result;
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
    if (node_identity(node)->uuid == uuid)
      found = node;
  });
  if (!found)
    return error::not_found;
  return found;
}

result<scene_node_info> world::scene_node_info_at(scene_instance_id instance, std::uint64_t index,
                                                  node_query_detail detail) {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  const auto nodes = scene_members(instance);
  if (index >= nodes.size()) {
    return engine::error::not_found;
  }
  const auto node = nodes[static_cast<std::size_t>(index)];
  std::uint64_t entity = UINT64_C(0);
  auto result = engine::write_identifier<entity_id>(entity, [&](auto& native_output) {
    return this->node_get_entity(gneiss::scene_node_id{node}, native_output);
  });
  if (result != engine::error::success || entity == UINT64_C(0)) {
    return result == engine::error::success ? engine::error::invalid_handle : result;
  }
  scene_node_id parent = null_scene_node_id;
  result = this->node_get_parent(gneiss::scene_node_id{node}, parent);
  if (result != engine::error::success) {
    return result;
  }
  scene_node_info out_info{};
  out_info.node = scene_node_id{node};
  out_info.parent = scene_node_id{parent};
  out_info.entity = entity_id{entity};
  out_info.uuid = node_identity(node)->uuid;
  out_info.name = node_identity(node)->name;
  if (detail != node_query_detail::identity) {
    auto components = capture_components(*this, node);
    if (!components)
      return components.error();
    out_info.components = std::move(*components);
  }
  if (detail == node_query_detail::components) {
    result = this->node_get_local_transform(node, out_info.local_transform);
    if (result.failed())
      return result;
  }
  return out_info;
}

result<scene_node_id> world::create_scene_node(scene_instance_id instance,
                                               const node_creation& desc) {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  if (!desc.uuid || find_scene_node(instance, desc.uuid))
    return error::invalid_argument;
  if (desc.parent && validate_scene_node(instance, desc.parent).failed())
    return error::invalid_handle;
  auto created =
      this->object_create(desc.parent, {desc.uuid, std::string(desc.name)}, desc.local_transform);
  if (!created)
    return created.error();
  try {
    state_->assign_scene(*created, instance);
  } catch (...) {
    (void)this->object_destroy(*created);
    throw;
  }
  return *created;
}

engine::error world::reparent_scene_node(scene_instance_id instance, scene_node_id node,
                                         scene_node_id parent) {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  if (validate_scene_node(instance, node).failed()) {
    return engine::error::invalid_handle;
  }
  if (parent && validate_scene_node(instance, parent).failed())
    return error::invalid_handle;
  return this->node_reparent(node, parent);
}

engine::error world::destroy_scene_subtree(scene_instance_id instance, scene_node_id root) {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  if (validate_scene_node(instance, root).failed())
    return error::invalid_handle;
  return this->object_destroy(root);
}

engine::error world::destroy_scene_node(scene_instance_id instance, scene_node_id node) {
  const auto status = validate_scene(instance);
  if (status.failed())
    return status;
  if (validate_scene_node(instance, node).failed())
    return error::invalid_handle;
  const auto* children = state_->scene().children(node);
  if (children && !children->empty())
    return error::invalid_state;
  return destroy_scene_subtree(instance, node);
}

} // namespace gneiss::engine
