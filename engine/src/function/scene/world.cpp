// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "function/scene/world_state.hpp"
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/function/scene/world.hpp>
namespace gneiss {
world_state& state_of(world& owner) noexcept {
  auto& state = static_cast<world_state&>(owner);
  GNEISS_ASSERT(state.is_owner_thread(), "World 内部访问要求所属线程");
  return state;
}
const world_state& state_of(const world& owner) noexcept {
  const auto& state = static_cast<const world_state&>(owner);
  GNEISS_ASSERT(state.is_owner_thread(), "World 内部访问要求所属线程");
  return state;
}

events::sink<void(const object_changes&)> world::changes() const {
  return state_of(*this).changes.changed();
}
void world::flush_changes() {
  GNEISS_ASSERT(is_owner_thread(), "World 变化通知必须在所属线程派发");
  state_of(*this).changes.flush();
}
result<scene_instance> world::create_scene() {
  if (!is_owner_thread())
    return error::invalid_state;
  const auto frozen = meta::is_frozen();
  if (!frozen || state_of(*this).scene_domain == 0U)
    return error::invalid_state;
  auto created =
      state_of(*this).scene_slots.create(resource_type::scene_instance, scene_instance_id{});
  if (!created)
    return created.error();
  *state_of(*this).scene_slots.get(*created, resource_type::scene_instance) = *created;
  return scene_instance::borrow(*this, *created);
}
bool world::is_owner_thread() const noexcept {
  return static_cast<const world_state&>(*this).is_owner_thread();
}

std::uint64_t world::entity_count() const {
  GNEISS_ASSERT(is_owner_thread(), "World 实体数量必须在所属线程查询");
  return static_cast<std::uint64_t>(state_of(*this).entity_count());
}

result<game_object> world::object(entity_id entity) noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  if (!state_of(*this).is_alive(entity.get()))
    return error::invalid_handle;
  if (!state_of(*this).get<detail::object_state>(entity.get()))
    return error::not_found;
  return find(entity);
}
}
