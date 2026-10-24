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

template <typename Function>
gneiss::engine::result with_state(gneiss::engine::world_state& state,
                                  Function&& function) noexcept {
  if (!state.is_owner_thread())
    return gneiss::engine::result::invalid_state;
  try {
    return function(state);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
}
namespace gneiss::engine {
world::world(std::uint32_t domain) : state_(std::make_unique<world_state>(domain)) {}
world::~world() noexcept = default;
bool world::is_owner_thread() const noexcept { return state_->is_owner_thread(); }
gneiss::engine::result world::begin_publication(gneiss::engine::publication& output) noexcept {
  output.reset();
  return with_state(*state_, [&](world_state& state) {
    output = state.begin_publication();
    return gneiss::engine::result::success;
  });
}
gneiss::engine::result world::publish(const gneiss::engine::publication& group) noexcept {
  return with_state(*state_, [&](world_state& state) { return state.publish(group); });
}
gneiss::engine::result
world::retire_publication(const gneiss::engine::publication& group) noexcept {
  return with_state(*state_, [&](world_state& state) { return state.retire(group); });
}
gneiss::engine::result
world::replace_publications(std::span<const gneiss::engine::publication> previous,
                            std::span<const gneiss::engine::publication> candidates) noexcept {
  return with_state(*state_, [&](world_state& state) {
    return state.replace_publications(previous, candidates);
  });
}
gneiss::engine::result world::bind_publication(const gneiss::engine::publication& group,
                                               staged_object object) noexcept {
  return with_state(*state_, [&](world_state& state) {
    return state.bind_publication(group, object.entity.get(), object.node.get());
  });
}
gneiss::engine::result world::create_staged_object(const gneiss::engine::publication& group,
                                                   const staged_object_description& description,
                                                   staged_object& output) noexcept {
  output = {};
  if (description.camera != nullptr &&
      !is_valid_camera(description.camera->vertical_field_of_view_radians,
                       description.camera->near_plane, description.camera->far_plane)) {
    return gneiss::engine::result::invalid_argument;
  }
  if (description.renderer != nullptr &&
      (description.renderer->mesh == gneiss::engine::null_rid ||
       description.renderer->material == gneiss::engine::null_rid)) {
    return gneiss::engine::result::invalid_argument;
  }
  return with_state(*state_, [&](world_state& state) {
    gneiss_entity_id candidate_entity{};
    gneiss::engine::rid candidate_node{};
    auto operation = gneiss::engine::result::success;
    try {
      candidate_entity = state.create_staged_entity(group);
      if (candidate_entity == GNEISS_NULL_ENTITY_ID) {
        return gneiss::engine::result::invalid_state;
      }
      if (description.camera != nullptr) {
        (void)state.emplace_staged<camera_component>(group, candidate_entity, *description.camera);
      }
      if (description.renderer != nullptr) {
        (void)state.emplace_staged<mesh_renderer_component>(
            group, candidate_entity,
            mesh_renderer{description.renderer->mesh, description.renderer->material});
      }
      operation = state.scene().create_staged(group, description.parent.get(), candidate_entity,
                                              &candidate_node);
      if (operation == gneiss::engine::result::success) {
        operation = state.scene().set_staged_local(group, candidate_node, description.transform);
      }
    } catch (const std::bad_alloc&) {
      operation = gneiss::engine::result::out_of_memory;
    } catch (...) {
      operation = gneiss::engine::result::internal;
    }
    if (operation == gneiss::engine::result::success) {
      output = {.entity = entity_id{candidate_entity}, .node = scene_node_id{candidate_node}};
      return operation;
    }
    if (candidate_node != gneiss::engine::null_rid) {
      (void)state.scene().destroy_staged_leaf(group, candidate_node);
    }
    if (candidate_entity != GNEISS_NULL_ENTITY_ID) {
      (void)state.destroy_staged_entity(group, candidate_entity);
    }
    return operation;
  });
}
gneiss::engine::result world::destroy_staged_object(const gneiss::engine::publication& group,
                                                    staged_object object) noexcept {
  return with_state(*state_, [&](world_state& state) {
    if (object.entity.get() == GNEISS_NULL_ENTITY_ID ||
        state.scene().get_staged_entity(group, object.node.get()) != object.entity.get()) {
      return gneiss::engine::result::invalid_handle;
    }
    const auto operation = state.scene().destroy_staged_leaf(group, object.node.get());
    if (operation != gneiss::engine::result::success) {
      return operation;
    }
    return state.destroy_staged_entity(group, object.entity.get())
               ? gneiss::engine::result::success
               : gneiss::engine::result::internal;
  });
}
gneiss::engine::result world::entity_create(entity_id& out_entity_value) noexcept {
  gneiss_entity_id out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_entity = GNEISS_NULL_ENTITY_ID;
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
  gneiss_entity_id entity = entity_value.get();
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
  gneiss_entity_id entity = entity_value.get();
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
                                                const camera_settings& camera,
                                                bool primary) noexcept {
  gneiss_entity_id entity = entity_value.get();
  if (!is_valid_camera(camera.vertical_field_of_view_radians, camera.near_plane,
                       camera.far_plane)) {
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
        entity, camera_settings{camera.vertical_field_of_view_radians, camera.near_plane,
                                camera.far_plane});
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
                                                      const camera_settings& desc) noexcept {
  gneiss_entity_id entity = entity_value.get();
  if (!is_valid_camera(desc.vertical_field_of_view_radians, desc.near_plane, desc.far_plane)) {
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
                                                camera_settings& out_camera) noexcept {
  gneiss_entity_id entity = entity_value.get();
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
    out_camera = {.vertical_field_of_view_radians = component->value.vertical_field_of_view_radians,
                  .near_plane = component->value.near_plane,
                  .far_plane = component->value.far_plane};
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
gneiss::engine::result world::entity_remove_camera(entity_id entity_value) noexcept {
  gneiss_entity_id entity = entity_value.get();
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
  gneiss_entity_id entity = entity_value.get();
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
  gneiss_entity_id out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_entity = GNEISS_NULL_ENTITY_ID;
    try {
      auto* state = state_.get();
      const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_state);
      if (thread_result != gneiss::engine::result::success) {
        return thread_result;
      }
      out_entity = state->active_camera();
      return out_entity == GNEISS_NULL_ENTITY_ID ? gneiss::engine::result::not_ready
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
    std::unordered_set<gneiss_entity_id> entities;
    for (const auto& change : replacements) {
      if (!entities.insert(change.entity.get()).second ||
          change.replacement.mesh == gneiss::engine::null_rid ||
          change.replacement.material == gneiss::engine::null_rid) {
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
  gneiss_entity_id entity = entity_value.get();
  if (renderer.mesh == gneiss::engine::null_rid || renderer.material == gneiss::engine::null_rid) {
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
  gneiss_entity_id entity = entity_value.get();
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
  gneiss::engine::rid parent = parent_value.get();
  gneiss_entity_id entity = entity_value.get();
  gneiss::engine::rid out_node = out_node_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_node = gneiss::engine::null_rid;
    try {
      auto* state = state_.get();
      const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_state);
      if (thread_result != gneiss::engine::result::success) {
        return thread_result;
      }
      if (entity != GNEISS_NULL_ENTITY_ID && !state->is_alive(entity)) {
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
  gneiss::engine::rid node = node_value.get();
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
  gneiss::engine::rid node = node_value.get();
  gneiss::engine::rid parent = parent_value.get();
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
                                const gneiss::engine::transform& transform) noexcept {
  gneiss::engine::rid node = node_value.get();
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
                                gneiss::engine::transform& out_transform) noexcept {
  gneiss::engine::rid node = node_value.get();
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
                                gneiss::engine::transform& out_transform) noexcept {
  gneiss::engine::rid node = node_value.get();
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? gneiss::engine::result::success
                                                  : gneiss::engine::result::invalid_state);
    if (thread_result != gneiss::engine::result::success)
      return thread_result;
    gneiss::engine::transform value;
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
  gneiss::engine::rid node = node_value.get();
  gneiss_entity_id out_entity = out_entity_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_entity = GNEISS_NULL_ENTITY_ID;
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
  gneiss::engine::rid node = node_value.get();
  gneiss::engine::rid out_parent = out_parent_value.get();
  const auto operation_result = [&]() -> gneiss::engine::result {
    out_parent = gneiss::engine::null_rid;
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
                                  gneiss::engine::transform& out_transform) noexcept {
  gneiss_entity_id entity = entity_value.get();
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
                                  const gneiss::engine::transform& transform) noexcept {
  gneiss_entity_id entity = entity_value.get();
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
