// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/render_snapshot.hpp"

#include "engine/function/world/world_state.hpp"

#include <new>

namespace gneiss::world_internal {
gneiss_result build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                                    std::uint32_t viewport_height,
                                    render_internal::render_snapshot& out_snapshot,
                                    const render_internal::view_camera* camera) noexcept {
  if (viewport_width == 0U || viewport_height == 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    render_internal::render_snapshot snapshot;
    render_internal::view_camera selected;
    bool has_camera = camera != nullptr;
    if (camera != nullptr) {
      selected = *camera;
    } else {
      const auto active_camera = world.active_camera();
      if (const auto* component = world.get<camera_component>(active_camera);
          component != nullptr) {
        selected.camera = {component->value.vertical_field_of_view_radians,
                           component->value.near_plane, component->value.far_plane};
        engine::transform transform;
        has_camera =
            world.scene().get_world_for_entity(active_camera, &transform) == GNEISS_SUCCESS;
        selected.transform = transform;
      }
    }
    if (has_camera) {
      render_internal::matrix4 view;
      render_internal::matrix4 projection;
      const auto view_result = render_internal::build_view_matrix(selected.transform, view);
      const auto aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
      const auto projection_result =
          render_internal::build_perspective_matrix(selected.camera, aspect, projection);
      if (view_result != GNEISS_SUCCESS) {
        return view_result;
      }
      if (projection_result != GNEISS_SUCCESS) {
        return projection_result;
      }
      snapshot.camera = {.camera = selected.camera,
                         .transform = selected.transform,
                         .view = view,
                         .projection = projection,
                         .viewport_width = viewport_width,
                         .viewport_height = viewport_height};
      snapshot.has_camera = true;
    }
    world.each<mesh_renderer_component>(
        [&](entt::entity native_entity, const mesh_renderer_component& component) {
          const auto entity = world.encode_entity(native_entity);
          engine::transform transform;
          if (world.scene().get_world_for_entity(entity, &transform) == GNEISS_SUCCESS) {
            snapshot.instances.push_back({.mesh = component.value.mesh,
                                          .material = component.value.material,
                                          .transform = transform});
          }
        });
    out_snapshot = std::move(snapshot);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::world_internal
