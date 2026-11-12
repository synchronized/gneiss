// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/world.hpp"
#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/detail/world_impl.hpp"
namespace gneiss {
result<detail::world_component_access::storage*>
detail::world_component_access::get(world& owner) noexcept {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  return owner.impl_.get();
}
world::world(std::uint32_t domain) : impl_(std::make_unique<implementation>(domain)) {}
world::~world() noexcept try {
  impl_->scene_slots.for_each(resource_type::scene_instance,
                              [&](scene_instance_id id) { impl_->destroy_scene_members(id); });
} catch (...) {
  fatal_current_exception();
}
events::sink<void(const object_changes&)> world::changes() const {
  return impl_->changes.changed();
}
result<void> world::flush_changes() {
  if (!is_owner_thread())
    return error::invalid_state;
  impl_->changes.flush();
  return {};
}
result<scene_instance> world::create_scene() {
  if (!is_owner_thread())
    return error::invalid_state;
  const auto frozen = meta::is_frozen();
  if (!frozen || !*frozen || impl_->scene_domain == 0U)
    return error::invalid_state;
  auto created = impl_->scene_slots.create(resource_type::scene_instance, scene_instance_id{});
  if (!created)
    return created.error();
  *impl_->scene_slots.get(*created, resource_type::scene_instance) = *created;
  return scene_instance::borrow(*this, *created);
}
bool world::is_owner_thread() const noexcept { return impl_->is_owner_thread(); }

result<std::uint64_t> world::entity_count() {
  {
    auto* state = impl_.get();
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
  if (!impl_->is_alive(entity.get()))
    return error::invalid_handle;
  if (!impl_->get<detail::object_state>(entity.get()))
    return error::not_found;
  return find(entity);
}
}
