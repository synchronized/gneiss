// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/core/fatal.hpp"
#include "engine/function/world/scene_instance.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/function/world/world_state.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace gneiss::engine {
result<scene_instance> scene_instance::borrow(world& owner, scene_instance_id id) noexcept {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  if (!owner.scenes())
    return error::invalid_handle;
  auto count = owner.scenes()->get_node_count(id);
  if (!count)
    return count.error();
  scene_instance result;
  result.owner_ = &owner;
  result.id_ = id;
  return result;
}
bool scene_instance::is_valid() const noexcept { return size().has_value(); }
result<std::uint64_t> scene_instance::size() const noexcept {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  return owner_->scenes()->get_node_count(id_);
}
result<game_object> scene_instance::find(guid uuid) const noexcept {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  auto node = owner_->scenes()->find_node(id_, uuid);
  if (!node)
    return node.error();
  return owner_->object(entity_id{node->get()});
}
result<game_object> scene_instance::create(scene_node_identity identity,
                                           const math::transform& transform,
                                           game_object parent) const {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  if (parent.owner() && parent.owner() != owner_)
    return error::invalid_handle;
  auto created = owner_->scenes()->create_node(id_, {.uuid = identity.uuid,
                                                     .name = identity.name,
                                                     .parent = scene_node_id{parent.entity().get()},
                                                     .local_transform = transform});
  if (!created)
    return created.error();
  return owner_->object(entity_id{created->get()});
}
error scene_instance::unload() const noexcept {
  if (!owner_ || !owner_->scenes())
    return error::invalid_handle;
  return owner_->scenes()->unload(id_);
}

namespace {
std::uint16_t allocate_domain() noexcept {
  static std::atomic_uint32_t next_domain{UINT32_C(1024)};
  const auto value = next_domain.fetch_add(1U, std::memory_order_relaxed);
  return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                            : UINT16_C(0);
}

} // namespace

result<scene_instances*> scene_instances::attach(world& owner) {
  if (!owner.is_owner_thread() || owner.scenes_)
    return error::invalid_state;
  auto candidate = std::unique_ptr<scene_instances>(new scene_instances(owner));
  if (!candidate->is_valid())
    return error::invalid_state;
  owner.scenes_ = std::move(candidate);
  return owner.scenes_.get();
}

scene_instances::scene_instances(world& native_world) noexcept
    : native_world_(native_world), domain_(allocate_domain()), instances_(domain_) {}

scene_instances::~scene_instances() noexcept try {
  instances_.for_each(resource_type::scene_instance,
                      [&](scene_instance_id id) { storage().destroy_scene_members(id); });
} catch (...) {
  fatal_current_exception();
}

error scene_instances::validate(scene_instance_id instance) const noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  return instances_.get(instance, resource_type::scene_instance) ? error::success
                                                                 : error::invalid_handle;
}

result<scene_instance_id> scene_instances::create_empty(guid scene_uuid) {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!is_valid())
    return error::invalid_argument;
  if (!scene_uuid)
    return error::invalid_argument;
  auto created = instances_.create(resource_type::scene_instance, scene_instance_id{});
  if (created)
    *instances_.get(*created, resource_type::scene_instance) = *created;
  return created;
}

error scene_instances::unload(scene_instance_id instance) noexcept try {
  const auto status = validate(instance);
  if (status.failed())
    return status;
  storage().destroy_scene_members(instance);
  return instances_.destroy(instance, resource_type::scene_instance);
} catch (...) {
  fatal_current_exception();
}

} // namespace gneiss::engine
