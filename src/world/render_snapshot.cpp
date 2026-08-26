// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "world/render_snapshot.h"

#include "world/world_state.h"

#include <new>

namespace gneiss::world_internal {

gneiss_result build_render_snapshot(world_state& world, render_snapshot& out_snapshot) noexcept {
  try {
    render_snapshot snapshot;
    world.each<camera_component>(
        [&](entt::entity native_entity, const camera_component& component) {
          if (snapshot.has_camera || component.value.is_primary == 0U) {
            return;
          }
          const auto entity = world.encode_entity(native_entity);
          gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
          if (world.scene().get_world_for_entity(entity, &transform) == GNEISS_SUCCESS) {
            snapshot.camera = {.camera = component.value, .transform = transform};
            snapshot.has_camera = true;
          }
        });
    world.each<mesh_renderer_component>(
        [&](entt::entity native_entity, const mesh_renderer_component& component) {
          const auto entity = world.encode_entity(native_entity);
          gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
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
