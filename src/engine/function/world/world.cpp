// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/render_snapshot.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/function/world/world_state.hpp"
#include <cmath>
#include <numbers>
#include <unordered_set>
namespace {
bool is_valid_camera(float vertical_field_of_view_radians, float near_plane,
                     float far_plane) noexcept {
  return std::isfinite(vertical_field_of_view_radians) && std::isfinite(near_plane) &&
         std::isfinite(far_plane) && vertical_field_of_view_radians > 0.0F &&
         vertical_field_of_view_radians < std::numbers::pi_v<float> && near_plane > 0.0F &&
         far_plane > near_plane;
}

}
namespace gneiss::engine {
result<world_state*> detail::world_component_access::get(world& owner) noexcept {
  if (!owner.is_owner_thread())
    return error::invalid_state;
  return owner.state_.get();
}
world::world(std::uint32_t domain) : state_(std::make_unique<world_state>(domain)) {}
world::~world() noexcept = default;
bool world::is_owner_thread() const noexcept { return state_->is_owner_thread(); }
result<scene_node_id> world::object_create(scene_node_id parent, scene_node_identity identity,
                                           const math::transform& transform) noexcept {
  entity_id entity;
  auto status = entity_create(entity);
  if (status.failed())
    return status;
  scene_node_id node;
  status = node_create(parent, entity, node);
  if (status.ok())
    status = node_set_local_transform(node, transform);
  if (status.ok())
    status = node_set_identity(node, std::move(identity));
  if (status.failed()) {
    (void)entity_destroy(entity);
    if (node)
      (void)node_destroy(node);
    return status;
  }
  return node;
}
error world::object_destroy(scene_node_id node) noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  const auto* children = state_->scene().children(node);
  if (!children)
    return error::invalid_handle;
  while (!children->empty()) {
    const auto status = object_destroy(children->back());
    if (status.failed())
      return status;
  }
  const auto entity = state_->scene().get_entity(node);
  if (entity != 0)
    (void)state_->destroy_entity(entity);
  return state_->scene().destroy(node);
}
error world::node_set_identity(scene_node_id node, scene_node_identity identity) noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  return state_->scene().set_identity(node, std::move(identity));
}
result<scene_node_identity_view> world::node_identity(scene_node_id node) const noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  return state_->scene().identity(node);
}
result<scene_node_id> world::entity_node(entity_id entity) const noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  if (!state_->is_alive(entity.get()))
    return error::invalid_handle;
  return state_->scene().node_for_entity(entity.get());
}
result<mesh_renderer> world::node_mesh_renderer(scene_node_id node) noexcept {
  if (!is_owner_thread())
    return error::invalid_state;
  if (!state_->scene().get_local(node))
    return error::invalid_handle;
  const auto entity = state_->scene().get_entity(node);
  const auto* component = state_->get<mesh_renderer_component>(entity);
  if (!component)
    return error::not_found;
  return component->value;
}

engine::error world::entity_create(entity_id& out_entity_value) noexcept {
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> engine::error {
    out_entity = UINT64_C(0);
    try {
      auto* state = state_.get();
      const auto thread_result =
          (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
      if (thread_result != engine::error::success) {
        return thread_result;
      }
      out_entity = state->create_entity();
      return engine::error::success;
    } catch (const std::bad_alloc&) {
      return engine::error::out_of_memory;
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}
engine::error world::entity_destroy(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return state->destroy_entity(entity) ? engine::error::success : engine::error::invalid_handle;
  } catch (...) {
    return engine::error::internal;
  }
}
result<bool> world::entity_is_alive(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return state->is_alive(entity);
  } catch (...) {
    return engine::error::internal;
  }
}
result<std::uint64_t> world::entity_count() noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return static_cast<std::uint64_t>(state->entity_count());
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::entity_set_camera(entity_id entity_value, const math::camera_settings& camera,
                                       bool primary) noexcept {
  std::uint64_t entity = entity_value.get();
  if (!is_valid_camera(camera.fov, camera.near, camera.far)) {
    return engine::error::invalid_argument;
  }
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return engine::error::invalid_handle;
    }
    state->emplace_or_replace<camera_component>(
        entity, math::camera_settings{camera.fov, camera.near, camera.far});
    if (primary) {
      return state->set_active_camera(entity);
    }
    state->clear_active_camera(entity);
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::entity_configure_camera(entity_id entity_value,
                                             const math::camera_settings& desc) noexcept {
  std::uint64_t entity = entity_value.get();
  if (!is_valid_camera(desc.fov, desc.near, desc.far)) {
    return engine::error::invalid_argument;
  }
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return engine::error::invalid_handle;
    }
    state->emplace_or_replace<camera_component>(entity, desc);
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::entity_get_camera(entity_id entity_value,
                                       math::camera_settings& out_camera) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return engine::error::invalid_handle;
    }
    const auto* component = state->get<camera_component>(entity);
    if (component == nullptr) {
      return engine::error::not_found;
    }
    out_camera = {
        .fov = component->value.fov, .near = component->value.near, .far = component->value.far};
    return engine::error::success;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::entity_remove_camera(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    return thread_result == engine::error::success ? state->clear_camera(entity) : thread_result;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::set_active_camera(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    return thread_result == engine::error::success ? state->set_active_camera(entity)
                                                   : thread_result;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::get_active_camera(entity_id& out_entity_value) noexcept {
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> engine::error {
    out_entity = UINT64_C(0);
    try {
      auto* state = state_.get();
      const auto thread_result =
          (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
      if (thread_result != engine::error::success) {
        return thread_result;
      }
      out_entity = state->active_camera();
      return out_entity == UINT64_C(0) ? engine::error::not_ready : engine::error::success;
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}
engine::error
world::replace_mesh_renderers(std::span<const mesh_renderer_replacement> replacements) noexcept {
  try {
    auto* state = state_.get();
    const auto validated =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (validated != engine::error::success) {
      return validated;
    }
    std::unordered_set<std::uint64_t> entities;
    for (const auto& change : replacements) {
      if (!entities.insert(change.entity.get()).second) {
        return engine::error::invalid_argument;
      }
      if (!state->is_alive(change.entity.get())) {
        return engine::error::invalid_handle;
      }
      const auto* current = state->get<mesh_renderer_component>(change.entity.get());
      if (current == nullptr || current->value.mesh != change.expected.mesh ||
          current->value.material != change.expected.material) {
        return engine::error::invalid_state;
      }
    }
    for (const auto& change : replacements) {
      state->get<mesh_renderer_component>(change.entity.get())->value = {
          .mesh = change.replacement.mesh,
          .material = change.replacement.material,
      };
    }
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::entity_set_mesh_renderer(entity_id entity_value,
                                              const mesh_renderer& renderer) noexcept {
  std::uint64_t entity = entity_value.get();

  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return engine::error::invalid_handle;
    }
    state->emplace_or_replace<mesh_renderer_component>(
        entity, mesh_renderer{renderer.mesh, renderer.material});
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error world::entity_remove_mesh_renderer(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    return thread_result == engine::error::success ? state->clear_mesh_renderer(entity)
                                                   : thread_result;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::node_create(scene_node_id parent_value, entity_id entity_value,
                                 scene_node_id& out_node_value) noexcept {
  scene_node_id parent = parent_value;
  std::uint64_t entity = entity_value.get();
  scene_node_id out_node = out_node_value;
  const auto operation_result = [&]() -> engine::error {
    out_node = null_scene_node_id;
    try {
      auto* state = state_.get();
      const auto thread_result =
          (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
      if (thread_result != engine::error::success) {
        return thread_result;
      }
      if (entity != UINT64_C(0) && !state->is_alive(entity)) {
        return engine::error::invalid_handle;
      }
      auto created = state->scene().create(parent, entity);
      if (!created)
        return created.error();
      out_node = *created;
      return error::success;
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_node_value = scene_node_id{out_node};
  return operation_result;
}
engine::error world::node_destroy(scene_node_id node_value) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    return thread_result == engine::error::success ? state->scene().destroy(node) : thread_result;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::node_reparent(scene_node_id node_value, scene_node_id parent_value) noexcept {
  scene_node_id node = node_value;
  scene_node_id parent = parent_value;
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    return thread_result == engine::error::success ? state->scene().reparent(node, parent)
                                                   : thread_result;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::node_set_local_transform(scene_node_id node_value,
                                              const math::transform& transform) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    return thread_result == engine::error::success ? state->scene().set_local(node, transform)
                                                   : thread_result;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::node_get_local_transform(scene_node_id node_value,
                                              math::transform& out_transform) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    const auto* transform = state->scene().get_local(node);
    if (transform == nullptr) {
      return engine::error::invalid_handle;
    }
    out_transform = *transform;
    return engine::error::success;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::node_get_world_transform(scene_node_id node_value,
                                              math::transform& out_transform) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success)
      return thread_result;
    auto value = state->scene().get_world(node);
    if (!value)
      return value.error();
    out_transform = *value;
    return error::success;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::node_get_entity(scene_node_id node_value,
                                     entity_id& out_entity_value) noexcept {
  scene_node_id node = node_value;
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> engine::error {
    out_entity = UINT64_C(0);
    try {
      auto* state = state_.get();
      const auto thread_result =
          (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
      if (thread_result != engine::error::success) {
        return thread_result;
      }
      const auto* transform = state->scene().get_local(node);
      if (transform == nullptr) {
        return engine::error::invalid_handle;
      }
      out_entity = state->scene().get_entity(node);
      return engine::error::success;
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}
engine::error world::node_get_parent(scene_node_id node_value,
                                     scene_node_id& out_parent_value) noexcept {
  scene_node_id node = node_value;
  scene_node_id out_parent = out_parent_value;
  const auto operation_result = [&]() -> engine::error {
    out_parent = null_scene_node_id;
    try {
      auto* state = state_.get();
      const auto thread_result =
          (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
      if (thread_result != engine::error::success) {
        return thread_result;
      }
      if (state->scene().get_local(node) == nullptr) {
        return engine::error::invalid_handle;
      }
      out_parent = state->scene().get_parent(node);
      return engine::error::success;
    } catch (...) {
      return engine::error::internal;
    }
  }();
  out_parent_value = scene_node_id{out_parent};
  return operation_result;
}
engine::error world::entity_get_local_transform(entity_id entity_value,
                                                math::transform& out_transform) noexcept {
  std::uint64_t entity = entity_value.get();
  out_transform = {};
  try {
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
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::entity_set_local_transform(entity_id entity_value,
                                                const math::transform& transform) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
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
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error world::get_render_snapshot(std::uint32_t viewport_width,
                                         std::uint32_t viewport_height,
                                         render_snapshot& out_snapshot,
                                         const view_camera* camera) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result =
        (is_owner_thread() ? engine::error::success : engine::error::invalid_state);
    if (thread_result != engine::error::success) {
      return thread_result;
    }
    return build_render_snapshot(*state, viewport_width, viewport_height, out_snapshot, camera);
  } catch (...) {
    return engine::error::internal;
  }
}
}
