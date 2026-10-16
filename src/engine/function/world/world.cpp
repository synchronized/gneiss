// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"
#include "engine/function/world/render_snapshot.hpp"
#include "engine/function/world/world_state.hpp"
#include <cmath>
#include <numbers>
#include <unordered_set>
namespace {
// 兼容 World 服务仍接收旧场景值；转换只在进入原生场景树时发生。
gneiss::engine::transform native_transform(const gneiss_transform& value) noexcept {
  return {{value.translation[0], value.translation[1], value.translation[2]},
          {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]},
          {value.scale[0], value.scale[1], value.scale[2]}};
}
bool is_valid_camera(float vertical_field_of_view_radians, float near_plane,
                     float far_plane) noexcept {
  return std::isfinite(vertical_field_of_view_radians) && std::isfinite(near_plane) &&
         std::isfinite(far_plane) && vertical_field_of_view_radians > 0.0F &&
         vertical_field_of_view_radians < std::numbers::pi_v<float> && near_plane > 0.0F &&
         far_plane > near_plane;
}

template <typename Function>
gneiss_result with_state(gneiss::world_internal::world_state& state, Function&& function) noexcept {
  if (!state.is_owner_thread())
    return GNEISS_ERROR_INVALID_STATE;
  try {
    return function(state);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
}
namespace gneiss::engine {
using namespace world_internal;
world::world(std::uint32_t domain) : state_(std::make_unique<world_state>(domain)) {}
world::~world() noexcept = default;
bool world::is_owner_thread() const noexcept { return state_->is_owner_thread(); }
gneiss_result world::begin_publication(core::publication& output) noexcept {
  output.reset();
  return with_state(*state_, [&](world_state& state) {
    output = state.begin_publication();
    return GNEISS_SUCCESS;
  });
}
gneiss_result world::publish(const core::publication& group) noexcept {
  return with_state(*state_, [&](world_state& state) { return state.publish(group); });
}
gneiss_result world::retire_publication(const core::publication& group) noexcept {
  return with_state(*state_, [&](world_state& state) { return state.retire(group); });
}
gneiss_result world::replace_publications(std::span<const core::publication> previous,
                                          std::span<const core::publication> candidates) noexcept {
  return with_state(*state_, [&](world_state& state) {
    return state.replace_publications(previous, candidates);
  });
}
gneiss_result world::bind_publication(const core::publication& group,
                                      staged_object object) noexcept {
  return with_state(*state_, [&](world_state& state) {
    return state.bind_publication(group, object.entity, object.node);
  });
}
gneiss_result world::create_staged_object(const core::publication& group,
                                          const staged_object_description& description,
                                          staged_object& output) noexcept {
  output = {};
  if (description.camera != nullptr &&
      !is_valid_camera(description.camera->vertical_field_of_view_radians,
                       description.camera->near_plane, description.camera->far_plane)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (description.renderer != nullptr && (description.renderer->mesh == GNEISS_NULL_MESH ||
                                          description.renderer->material == GNEISS_NULL_MATERIAL)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return with_state(*state_, [&](world_state& state) {
    staged_object candidate;
    auto operation = GNEISS_SUCCESS;
    try {
      candidate.entity = state.create_staged_entity(group);
      if (candidate.entity == GNEISS_NULL_ENTITY_ID) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      if (description.camera != nullptr) {
        (void)state.emplace_staged<camera_component>(group, candidate.entity, *description.camera);
      }
      if (description.renderer != nullptr) {
        (void)state.emplace_staged<mesh_renderer_component>(
            group, candidate.entity,
            mesh_renderer{description.renderer->mesh, description.renderer->material});
      }
      operation =
          state.scene().create_staged(group, description.parent, candidate.entity, &candidate.node);
      if (operation == GNEISS_SUCCESS) {
        operation = state.scene().set_staged_local(group, candidate.node,
                                                   native_transform(description.transform));
      }
    } catch (const std::bad_alloc&) {
      operation = GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      operation = GNEISS_ERROR_INTERNAL;
    }
    if (operation == GNEISS_SUCCESS) {
      output = candidate;
      return operation;
    }
    if (candidate.node != GNEISS_NULL_SCENE_NODE_ID) {
      (void)state.scene().destroy_staged_leaf(group, candidate.node);
    }
    if (candidate.entity != GNEISS_NULL_ENTITY_ID) {
      (void)state.destroy_staged_entity(group, candidate.entity);
    }
    return operation;
  });
}
gneiss_result world::destroy_staged_object(const core::publication& group,
                                           staged_object object) noexcept {
  return with_state(*state_, [&](world_state& state) {
    if (object.entity == GNEISS_NULL_ENTITY_ID ||
        state.scene().get_staged_entity(group, object.node) != object.entity) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    const auto operation = state.scene().destroy_staged_leaf(group, object.node);
    if (operation != GNEISS_SUCCESS) {
      return operation;
    }
    return state.destroy_staged_entity(group, object.entity) ? GNEISS_SUCCESS
                                                             : GNEISS_ERROR_INTERNAL;
  });
}
gneiss_result world::entity_create(gneiss_entity_id& out_entity) noexcept {
  out_entity = GNEISS_NULL_ENTITY_ID;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    out_entity = state->create_entity();
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_destroy(gneiss_entity_id entity) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    return state->destroy_entity(entity) ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_HANDLE;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_is_alive(gneiss_entity_id entity, bool& out_is_alive) noexcept {
  out_is_alive = false;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    out_is_alive = state->is_alive(entity);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_count(uint64_t& out_count) noexcept {
  out_count = 0;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    out_count = static_cast<uint64_t>(state->entity_count());
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_set_camera(gneiss_entity_id entity, const camera_settings& camera,
                                       bool primary) noexcept {
  if (!is_valid_camera(camera.vertical_field_of_view_radians, camera.near_plane,
                       camera.far_plane)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    state->emplace_or_replace<camera_component>(
        entity, camera_settings{camera.vertical_field_of_view_radians, camera.near_plane,
                                camera.far_plane});
    if (primary) {
      return state->set_active_camera(entity);
    }
    state->clear_active_camera(entity);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_configure_camera(gneiss_entity_id entity,
                                             const camera_settings& desc) noexcept {
  if (!is_valid_camera(desc.vertical_field_of_view_radians, desc.near_plane, desc.far_plane)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    state->emplace_or_replace<camera_component>(entity, desc);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_get_camera(gneiss_entity_id entity,
                                       camera_settings& out_camera) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    const auto* component = state->get<gneiss::world_internal::camera_component>(entity);
    if (component == nullptr) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    out_camera = {.vertical_field_of_view_radians = component->value.vertical_field_of_view_radians,
                  .near_plane = component->value.near_plane,
                  .far_plane = component->value.far_plane};
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_remove_camera(gneiss_entity_id entity) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    return thread_result == GNEISS_SUCCESS ? state->clear_camera(entity) : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::set_active_camera(gneiss_entity_id entity) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    return thread_result == GNEISS_SUCCESS ? state->set_active_camera(entity) : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::get_active_camera(gneiss_entity_id& out_entity) noexcept {
  out_entity = GNEISS_NULL_ENTITY_ID;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    out_entity = state->active_camera();
    return out_entity == GNEISS_NULL_ENTITY_ID ? GNEISS_ERROR_NOT_READY : GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result
world::replace_mesh_renderers(std::span<const mesh_renderer_replacement> replacements) noexcept {
  try {
    auto* state = state_.get();
    const auto validated = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (validated != GNEISS_SUCCESS) {
      return validated;
    }
    std::unordered_set<gneiss_entity_id> entities;
    for (const auto& change : replacements) {
      if (!entities.insert(change.entity).second || change.replacement.mesh == GNEISS_NULL_MESH ||
          change.replacement.material == GNEISS_NULL_MATERIAL) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      if (!state->is_alive(change.entity)) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
      const auto* current = state->get<mesh_renderer_component>(change.entity);
      if (current == nullptr || current->value.mesh != change.expected.mesh ||
          current->value.material != change.expected.material) {
        return GNEISS_ERROR_INVALID_STATE;
      }
    }
    for (const auto& change : replacements) {
      state->get<mesh_renderer_component>(change.entity)->value = {change.replacement.mesh,
                                                                   change.replacement.material};
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_set_mesh_renderer(gneiss_entity_id entity,
                                              const mesh_renderer& renderer) noexcept {
  if (renderer.mesh == GNEISS_NULL_MESH || renderer.material == GNEISS_NULL_MATERIAL) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    state->emplace_or_replace<mesh_renderer_component>(
        entity, mesh_renderer{renderer.mesh, renderer.material});
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_remove_mesh_renderer(gneiss_entity_id entity) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    return thread_result == GNEISS_SUCCESS ? state->clear_mesh_renderer(entity) : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_create(gneiss_scene_node_id parent, gneiss_entity_id entity,
                                 gneiss_scene_node_id& out_node) noexcept {
  out_node = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (entity != GNEISS_NULL_ENTITY_ID && !state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    return state->scene().create(parent, entity, &out_node);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_destroy(gneiss_scene_node_id node) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    return thread_result == GNEISS_SUCCESS ? state->scene().destroy(node) : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_reparent(gneiss_scene_node_id node,
                                   gneiss_scene_node_id parent) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    return thread_result == GNEISS_SUCCESS ? state->scene().reparent(node, parent) : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_set_local_transform(gneiss_scene_node_id node,
                                              const engine::transform& transform) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    return thread_result == GNEISS_SUCCESS ? state->scene().set_local(node, transform)
                                           : thread_result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_get_local_transform(gneiss_scene_node_id node,
                                              engine::transform& out_transform) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    const auto* transform = state->scene().get_local(node);
    if (transform == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    out_transform = *transform;
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_get_world_transform(gneiss_scene_node_id node,
                                              engine::transform& out_transform) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS)
      return thread_result;
    engine::transform value;
    const auto result = state->scene().get_world(node, &value);
    if (result == GNEISS_SUCCESS)
      out_transform = value;
    return result;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_get_entity(gneiss_scene_node_id node,
                                     gneiss_entity_id& out_entity) noexcept {
  out_entity = GNEISS_NULL_ENTITY_ID;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    const auto* transform = state->scene().get_local(node);
    if (transform == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    out_entity = state->scene().get_entity(node);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::node_get_parent(gneiss_scene_node_id node,
                                     gneiss_scene_node_id& out_parent) noexcept {
  out_parent = GNEISS_NULL_SCENE_NODE_ID;
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (state->scene().get_local(node) == nullptr) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    out_parent = state->scene().get_parent(node);
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_get_local_transform(gneiss_entity_id entity,
                                                engine::transform& out_transform) noexcept {
  out_transform = {};
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    const auto* transform = state->scene().get_local_for_entity(entity);
    if (transform == nullptr) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    out_transform = *transform;
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::entity_set_local_transform(gneiss_entity_id entity,
                                                const engine::transform& transform) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    if (!state->is_alive(entity)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    return state->scene().set_local_for_entity(entity, transform);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
gneiss_result world::get_render_snapshot(std::uint32_t viewport_width,
                                         std::uint32_t viewport_height,
                                         render_internal::render_snapshot& out_snapshot,
                                         const render_internal::view_camera* camera) noexcept {
  try {
    auto* state = state_.get();
    const auto thread_result = (is_owner_thread() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
    if (thread_result != GNEISS_SUCCESS) {
      return thread_result;
    }
    return build_render_snapshot(*state, viewport_width, viewport_height, out_snapshot, camera);
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
}
