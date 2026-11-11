// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/world.hpp"
#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/detail/world_state.hpp"
namespace gneiss {
result<world_state*> detail::world_component_access::get(world& owner) noexcept {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  return owner.state_.get();
}
world::world(std::uint32_t domain) : state_(std::make_unique<world_state>(domain)) {}
world::~world() noexcept try {
  state_->scene_slots.for_each(resource_type::scene_instance,
                               [&](scene_instance_id id) { state_->destroy_scene_members(id); });
} catch (...) {
  fatal_current_exception();
}
result<scene_instance> world::create_scene() {
  if (!is_owner_thread())
    return error::invalid_state;
  const auto frozen = meta::is_frozen();
  if (!frozen || !*frozen || state_->scene_domain == 0U)
    return error::invalid_state;
  auto created = state_->scene_slots.create(resource_type::scene_instance, scene_instance_id{});
  if (!created)
    return created.error();
  *state_->scene_slots.get(*created, resource_type::scene_instance) = *created;
  return scene_instance::borrow(*this, *created);
}
bool world::is_owner_thread() const noexcept { return state_->is_owner_thread(); }

result<std::uint64_t> world::entity_count() {
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? gneiss::error::success : gneiss::error::invalid_state);
    if (thread_result != gneiss::error::success) {
      return thread_result;
    }
    return static_cast<std::uint64_t>(state->entity_count());
  }
}

result<game_object> world::object(entity_id entity) noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  if (!state_->is_alive(entity.get()))
    return error::invalid_handle;
  if (!state_->scene().get_local(entity))
    return error::not_found;
  return find(entity);
}
}
