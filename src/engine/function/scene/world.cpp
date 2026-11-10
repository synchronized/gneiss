// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/world.hpp"
#include "engine/core/error.hpp"
#include "engine/core/fatal.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/detail/render_extraction.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/detail/world_state.hpp"
namespace gneiss::engine {
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

engine::error world::entity_create(entity_id& out_entity_value) {
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> engine::error {
    out_entity = UINT64_C(0);
    {
      auto* state = state_.get();
      const auto thread_result =
          (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
      if (thread_result != engine::error::success) {
        return thread_result;
      }
      out_entity = state->create_entity();
      return engine::error::success;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}
engine::error world::entity_destroy(entity_id entity_value) {
  std::uint64_t entity = entity_value.get();
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return state->destroy_entity(entity) ? engine::error::success : engine::error::invalid_handle;
  }
}
result<bool> world::entity_is_alive(entity_id entity_value) {
  std::uint64_t entity = entity_value.get();
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return state->is_alive(entity);
  }
}
result<std::uint64_t> world::entity_count() {
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return static_cast<std::uint64_t>(state->entity_count());
  }
}

engine::error world::set_active_camera(entity_id entity_value) {
  std::uint64_t entity = entity_value.get();
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    return thread_result == engine::error::success ? state->set_active_camera(entity)
                                                   : thread_result;
  }
}
engine::error world::get_active_camera(entity_id& out_entity_value) {
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> engine::error {
    out_entity = UINT64_C(0);
    {
      auto* state = state_.get();
      const auto thread_result =
          (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
      if (thread_result != engine::error::success) {
        return thread_result;
      }
      out_entity = state->active_camera();
      return out_entity == UINT64_C(0) ? engine::error::not_ready : engine::error::success;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}

engine::error world::entity_get_local_transform(entity_id entity_value,
                                                math::transform& out_transform) {
  std::uint64_t entity = entity_value.get();
  out_transform = {};
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return engine::error::invalid_handle;
    }
    const auto* transform = state->scene().get_local_for_entity(entity);
    if (transform == nullptr) {
      return engine::error::not_found;
    }
    out_transform = *transform;
    return engine::error::success;
  }
}
engine::error world::entity_set_local_transform(entity_id entity_value,
                                                const math::transform& transform) {
  std::uint64_t entity = entity_value.get();
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return engine::error::invalid_handle;
    }
    return state->scene().set_local_for_entity(entity, transform);
  }
}
engine::error world::get_render_snapshot(std::uint32_t viewport_width,
                                         std::uint32_t viewport_height,
                                         render_snapshot& out_snapshot, const view_camera* camera) {
  {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return build_render_snapshot(*state, viewport_width, viewport_height, out_snapshot, camera);
  }
}

result<game_object> world::object(entity_id entity) noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  if (!state_->is_alive(entity.get()))
    return error::invalid_handle;
  if (!state_->scene().get_entity(entity))
    return error::not_found;
  return find(entity);
}
result<game_object> world::create_object(scene_node_identity identity,
                                         const math::transform& transform, game_object parent) {
  if (!is_owner_thread())
    return error::invalid_state;
  if (parent.owner() && parent.owner() != this)
    return error::invalid_handle;
  auto created = state_->scene().create(parent.entity(), 0);
  if (!created)
    return created.error();
  try {
    auto status = state_->scene().set_local(*created, transform);
    if (status.ok())
      status = state_->scene().set_identity(*created, std::move(identity));
    if (status.failed()) {
      (void)state_->destroy_entity(created->get());
      return status;
    }
  } catch (...) {
    (void)state_->destroy_entity(created->get());
    throw;
  }
  return find(*created);
}
}
