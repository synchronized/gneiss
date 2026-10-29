// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/render_snapshot.hpp"
#include "engine/core/error.hpp"

#include "engine/function/world/world_state.hpp"

#include <new>

namespace gneiss::engine {
error build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                            std::uint32_t viewport_height, render_snapshot& out_snapshot,
                            const view_camera* camera) noexcept {
  if (viewport_width == 0U || viewport_height == 0U) {
    return error::invalid_argument;
  }
  try {
    render_snapshot snapshot;
    view_camera selected;
    bool has_camera = camera != nullptr;
    if (camera != nullptr) {
      selected = *camera;
    } else {
      const auto active_camera = world.active_camera();
      if (const auto* component = world.get<camera_component>(active_camera);
          component != nullptr) {
        selected.camera = {component->value.fov, component->value.near, component->value.far};
        auto transform = world.scene().get_world_for_entity(active_camera);
        has_camera = transform.ok();
        if (transform)
          selected.transform = *transform;
      }
    }
    if (has_camera) {
      math::matrix4 view;
      math::matrix4 projection;
      const auto view_result = math::view_matrix_rh(selected.transform, view);
      const auto aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
      const auto projection_result = math::perspective_rh_zo(selected.camera, aspect, projection);
      if (view_result != error::success) {
        return view_result;
      }
      if (projection_result != error::success) {
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
          if (auto transform = world.scene().get_world_for_entity(entity)) {
            snapshot.instances.push_back({.mesh = component.value.mesh,
                                          .material = component.value.material,
                                          .transform = *transform});
          }
        });
    out_snapshot = std::move(snapshot);
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}

} // namespace gneiss::engine
