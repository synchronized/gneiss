// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/render_snapshot.hpp"
#include "engine/core/result.hpp"

#include "engine/function/world/world_state.hpp"

#include <new>

namespace gneiss::engine {
gneiss::engine::result build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                                             std::uint32_t viewport_height,
                                             gneiss::engine::render_snapshot& out_snapshot,
                                             const gneiss::engine::view_camera* camera) noexcept {
  if (viewport_width == 0U || viewport_height == 0U) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    gneiss::engine::render_snapshot snapshot;
    gneiss::engine::view_camera selected;
    bool has_camera = camera != nullptr;
    if (camera != nullptr) {
      selected = *camera;
    } else {
      const auto active_camera = world.active_camera();
      if (const auto* component = world.get<camera_component>(active_camera);
          component != nullptr) {
        selected.camera = {component->value.fov, component->value.near, component->value.far};
        gneiss::engine::transform transform;
        has_camera = world.scene().get_world_for_entity(active_camera, &transform) ==
                     gneiss::engine::result::success;
        selected.transform = transform;
      }
    }
    if (has_camera) {
      gneiss::engine::matrix4 view;
      gneiss::engine::matrix4 projection;
      const auto view_result = gneiss::engine::view_matrix_rh(selected.transform, view);
      const auto aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
      const auto projection_result =
          gneiss::engine::perspective_rh_zo(selected.camera, aspect, projection);
      if (view_result != gneiss::engine::result::success) {
        return view_result;
      }
      if (projection_result != gneiss::engine::result::success) {
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
          gneiss::engine::transform transform;
          if (world.scene().get_world_for_entity(entity, &transform) ==
              gneiss::engine::result::success) {
            snapshot.instances.push_back({.mesh = component.value.mesh,
                                          .material = component.value.material,
                                          .transform = transform});
          }
        });
    out_snapshot = std::move(snapshot);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

} // namespace gneiss::engine
