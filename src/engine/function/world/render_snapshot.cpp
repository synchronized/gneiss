// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/render_snapshot.hpp"
#include "engine/core/result.hpp"

#include "engine/function/world/world_state.hpp"

#include <new>

namespace gneiss::engine::world {
gneiss::engine::core::result
build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                      std::uint32_t viewport_height, engine::render::render_snapshot& out_snapshot,
                      const engine::render::view_camera* camera) noexcept {
  if (viewport_width == 0U || viewport_height == 0U) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    engine::render::render_snapshot snapshot;
    engine::render::view_camera selected;
    bool has_camera = camera != nullptr;
    if (camera != nullptr) {
      selected = *camera;
    } else {
      const auto active_camera = world.active_camera();
      if (const auto* component = world.get<camera_component>(active_camera);
          component != nullptr) {
        selected.camera = {component->value.vertical_field_of_view_radians,
                           component->value.near_plane, component->value.far_plane};
        engine::core::math::transform transform;
        has_camera = world.scene().get_world_for_entity(active_camera, &transform) ==
                     gneiss::engine::core::result::success;
        selected.transform = transform;
      }
    }
    if (has_camera) {
      engine::render::matrix4 view;
      engine::render::matrix4 projection;
      const auto view_result = engine::render::build_view_matrix(selected.transform, view);
      const auto aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
      const auto projection_result =
          engine::render::build_perspective_matrix(selected.camera, aspect, projection);
      if (view_result != gneiss::engine::core::result::success) {
        return view_result;
      }
      if (projection_result != gneiss::engine::core::result::success) {
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
          engine::core::math::transform transform;
          if (world.scene().get_world_for_entity(entity, &transform) ==
              gneiss::engine::core::result::success) {
            snapshot.instances.push_back({.mesh = component.value.mesh,
                                          .material = component.value.material,
                                          .transform = transform});
          }
        });
    out_snapshot = std::move(snapshot);
    return gneiss::engine::core::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::engine::world
