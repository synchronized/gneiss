// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/render_snapshot.hpp"
#include "engine/core/result.hpp"

#include "engine/function/world/world_state.hpp"

#include <new>

namespace gneiss::engine {
result build_render_snapshot(world_state& world, std::uint32_t viewport_width,
                             std::uint32_t viewport_height, render_snapshot& out_snapshot,
                             const view_camera* camera) noexcept {
  if (viewport_width == 0U || viewport_height == 0U) {
    return result::invalid_argument;
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
        math::transform transform;
        has_camera =
            world.scene().get_world_for_entity(active_camera, &transform) == result::success;
        selected.transform = transform;
      }
    }
    if (has_camera) {
      math::matrix4 view;
      math::matrix4 projection;
      const auto view_result = math::view_matrix_rh(selected.transform, view);
      const auto aspect = static_cast<float>(viewport_width) / static_cast<float>(viewport_height);
      const auto projection_result = math::perspective_rh_zo(selected.camera, aspect, projection);
      if (view_result != result::success) {
        return view_result;
      }
      if (projection_result != result::success) {
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
          math::transform transform;
          if (world.scene().get_world_for_entity(entity, &transform) == result::success) {
            snapshot.instances.push_back({.mesh = component.value.mesh,
                                          .material = component.value.material,
                                          .transform = transform});
          }
        });
    out_snapshot = std::move(snapshot);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

} // namespace gneiss::engine
