// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/render_snapshot.hpp"
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
world::world(std::uint32_t domain) : state_(std::make_unique<world_state>(domain)) {}
world::~world() noexcept = default;
bool world::is_owner_thread() const noexcept { return state_->is_owner_thread(); }
gneiss::engine::result world::entity_create(entity_id& out_entity_value) noexcept {
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_entity = UINT64_C(0);
    try {
      auto* state = state_.get();
      const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_state);
      if (thread_result != gneiss::engine::result::success) {
        return thread_result;
      }
      out_entity = state->create_entity();
      return gneiss::engine::result::success;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}
gneiss::engine::result world::entity_destroy(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    return state->destroy_entity(entity) ? gneiss::engine::result::success
                                         : gneiss::engine::result::invalid_handle;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_is_alive(entity_id entity_value, bool& out_is_alive) noexcept {
  std::uint64_t entity = entity_value.get();
  out_is_alive = false;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    out_is_alive = state->is_alive(entity);
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_count(uint64_t& out_count) noexcept {
  out_count = 0;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    out_count = static_cast<uint64_t>(state->entity_count());
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_set_camera(entity_id entity_value,
                                                const math::camera_settings& camera,
                                                bool primary) noexcept {
  std::uint64_t entity = entity_value.get();
  if (!is_valid_camera(camera.fov, camera.near, camera.far)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    state->emplace_or_replace<camera_component>(
        entity, math::camera_settings{camera.fov, camera.near, camera.far});
    if (primary) {
      return state->set_active_camera(entity);
    }
    state->clear_active_camera(entity);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_configure_camera(entity_id entity_value,
                                                      const math::camera_settings& desc) noexcept {
  std::uint64_t entity = entity_value.get();
  if (!is_valid_camera(desc.fov, desc.near, desc.far)) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    state->emplace_or_replace<camera_component>(entity, desc);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_get_camera(entity_id entity_value,
                                                math::camera_settings& out_camera) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    const auto* component = state->get<gneiss::engine::camera_component>(entity);
    if (component == nullptr) {
      return gneiss::engine::result::not_found;
    }
    out_camera = {
        .fov = component->value.fov, .near = component->value.near, .far = component->value.far};
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_remove_camera(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    return thread_result == gneiss::engine::result::success ? state->clear_camera(entity)
                                                            : thread_result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::set_active_camera(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    return thread_result == gneiss::engine::result::success ? state->set_active_camera(entity)
                                                            : thread_result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::get_active_camera(entity_id& out_entity_value) noexcept {
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_entity = UINT64_C(0);
    try {
      auto* state = state_.get();
      const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_state);
      if (thread_result != gneiss::engine::result::success) {
        return thread_result;
      }
      out_entity = state->active_camera();
      return out_entity == UINT64_C(0) ? gneiss::engine::result::not_ready
                                       : gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}
gneiss::engine::result
world::replace_mesh_renderers(std::span<const mesh_renderer_replacement> replacements) noexcept {
  try {
    auto* state = state_.get();
    const auto validated = (is_owner_thread() ? gneiss::engine::result::success
                                              : gneiss::engine::result::invalid_state);
    if (validated != gneiss::engine::result::success) {
      return validated;
    }
    std::unordered_set<std::uint64_t> entities;
    for (const auto& change : replacements) {
      if (!entities.insert(change.entity.get()).second || !change.replacement.mesh ||
          !change.replacement.material) {
        return gneiss::engine::result::invalid_argument;
      }
      if (!state->is_alive(change.entity.get())) {
        return gneiss::engine::result::invalid_handle;
      }
      const auto* current = state->get<mesh_renderer_component>(change.entity.get());
      if (current == nullptr || current->value.mesh != change.expected.mesh ||
          current->value.material != change.expected.material) {
        return gneiss::engine::result::invalid_state;
      }
    }
    for (const auto& change : replacements) {
      state->get<mesh_renderer_component>(change.entity.get())->value = {
          .mesh = change.replacement.mesh,
          .material = change.replacement.material,
      };
    }
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_set_mesh_renderer(entity_id entity_value,
                                                       const mesh_renderer& renderer) noexcept {
  std::uint64_t entity = entity_value.get();
  if (!renderer.mesh || !renderer.material) {
    return gneiss::engine::result::invalid_argument;
  }

  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    state->emplace_or_replace<mesh_renderer_component>(
        entity, mesh_renderer{renderer.mesh, renderer.material});
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result world::entity_remove_mesh_renderer(entity_id entity_value) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    return thread_result == gneiss::engine::result::success ? state->clear_mesh_renderer(entity)
                                                            : thread_result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::node_create(scene_node_id parent_value, entity_id entity_value,
                                          scene_node_id& out_node_value) noexcept {
  scene_node_id parent = parent_value;
  std::uint64_t entity = entity_value.get();
  scene_node_id out_node = out_node_value;
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_node = null_scene_node_id;
    try {
      auto* state = state_.get();
      const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_state);
      if (thread_result != gneiss::engine::result::success) {
        return thread_result;
      }
      if (entity != UINT64_C(0) && !state->is_alive(entity)) {
        return gneiss::engine::result::invalid_handle;
      }
      return state->scene().create(parent, entity, &out_node);
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_node_value = scene_node_id{out_node};
  return operation_result;
}
gneiss::engine::result world::node_destroy(scene_node_id node_value) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    return thread_result == gneiss::engine::result::success ? state->scene().destroy(node)
                                                            : thread_result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::node_reparent(scene_node_id node_value,
                                            scene_node_id parent_value) noexcept {
  scene_node_id node = node_value;
  scene_node_id parent = parent_value;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    return thread_result == gneiss::engine::result::success ? state->scene().reparent(node, parent)
                                                            : thread_result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result
world::node_set_local_transform(scene_node_id node_value,
                                const gneiss::engine::math::transform& transform) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    return thread_result == gneiss::engine::result::success
               ? state->scene().set_local(node, transform)
               : thread_result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result
world::node_get_local_transform(scene_node_id node_value,
                                gneiss::engine::math::transform& out_transform) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    const auto* transform = state->scene().get_local(node);
    if (transform == nullptr) {
      return gneiss::engine::result::invalid_handle;
    }
    out_transform = *transform;
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result
world::node_get_world_transform(scene_node_id node_value,
                                gneiss::engine::math::transform& out_transform) noexcept {
  scene_node_id node = node_value;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success)
      return thread_result;
    gneiss::engine::math::transform value;
    const auto result = state->scene().get_world(node, &value);
    if (result == gneiss::engine::result::success)
      out_transform = value;
    return result;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::node_get_entity(scene_node_id node_value,
                                              entity_id& out_entity_value) noexcept {
  scene_node_id node = node_value;
  std::uint64_t out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_entity = UINT64_C(0);
    try {
      auto* state = state_.get();
      const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_state);
      if (thread_result != gneiss::engine::result::success) {
        return thread_result;
      }
      const auto* transform = state->scene().get_local(node);
      if (transform == nullptr) {
        return gneiss::engine::result::invalid_handle;
      }
      out_entity = state->scene().get_entity(node);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_entity_value = entity_id{out_entity};
  return operation_result;
}
gneiss::engine::result world::node_get_parent(scene_node_id node_value,
                                              scene_node_id& out_parent_value) noexcept {
  scene_node_id node = node_value;
  scene_node_id out_parent = out_parent_value;
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_parent = null_scene_node_id;
    try {
      auto* state = state_.get();
      const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_state);
      if (thread_result != gneiss::engine::result::success) {
        return thread_result;
      }
      if (state->scene().get_local(node) == nullptr) {
        return gneiss::engine::result::invalid_handle;
      }
      out_parent = state->scene().get_parent(node);
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }();
  out_parent_value = scene_node_id{out_parent};
  return operation_result;
}
gneiss::engine::result
world::entity_get_local_transform(entity_id entity_value,
                                  gneiss::engine::math::transform& out_transform) noexcept {
  std::uint64_t entity = entity_value.get();
  out_transform = {};
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    const auto* transform = state->scene().get_local_for_entity(entity);
    if (transform == nullptr) {
      return gneiss::engine::result::not_found;
    }
    out_transform = *transform;
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result
world::entity_set_local_transform(entity_id entity_value,
                                  const gneiss::engine::math::transform& transform) noexcept {
  std::uint64_t entity = entity_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return gneiss::engine::result::invalid_handle;
    }
    return state->scene().set_local_for_entity(entity, transform);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result
world::get_render_snapshot(std::uint32_t viewport_width, std::uint32_t viewport_height,
                           gneiss::engine::render_snapshot& out_snapshot,
                           const gneiss::engine::view_camera* camera) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success) {
      return thread_result;
    }
    return build_render_snapshot(*state, viewport_width, viewport_height, out_snapshot, camera);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
}
