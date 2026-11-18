// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/assert.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include "function/scene/detail/world_component_access.hpp"
#include "function/scene/detail/world_impl.hpp"
namespace gneiss {
detail::world_component_access::storage&
detail::world_component_access::get(world& owner) noexcept {
  GNEISS_ASSERT(owner.is_owner_thread(), "World 内部访问要求所属线程");
  return *owner.impl_;
}
world::world(std::uint32_t domain) : impl_(std::make_unique<implementation>(domain)) {}
world::~world() noexcept {
  try {
    impl_->scene_slots.for_each(resource_type::scene_instance,
                                [&](scene_instance_id id) { impl_->destroy_scene_members(id); });
  } catch (...) {
    fatal_current_exception();
  }
}
events::sink<void(const object_changes&)> world::changes() const {
  return impl_->changes.changed();
}
void world::flush_changes() {
  GNEISS_ASSERT(is_owner_thread(), "World 变化通知必须在所属线程派发");
  impl_->changes.flush();
}
result<scene_instance> world::create_scene() {
  if (!is_owner_thread())
    return error::invalid_state;
  const auto frozen = meta::is_frozen();
  if (!frozen || impl_->scene_domain == 0U)
    return error::invalid_state;
  auto created = impl_->scene_slots.create(resource_type::scene_instance, scene_instance_id{});
  if (!created)
    return created.error();
  *impl_->scene_slots.get(*created, resource_type::scene_instance) = *created;
  return scene_instance::borrow(*this, *created);
}
bool world::is_owner_thread() const noexcept { return impl_->is_owner_thread(); }

std::uint64_t world::entity_count() const {
  GNEISS_ASSERT(is_owner_thread(), "World 实体数量必须在所属线程查询");
  return static_cast<std::uint64_t>(impl_->entity_count());
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
