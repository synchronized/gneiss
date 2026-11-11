// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/render/render_extraction.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/rendering/render_camera.hpp"
namespace gneiss {
result<render_snapshot> extract_render_snapshot(world& owner, std::uint32_t viewport_width,
                                                std::uint32_t viewport_height,
                                                const view_camera* camera) {
  auto state = detail::world_component_access::get(owner);
  if (!state)
    return state.error();
  auto& world = **state;
  if (viewport_width == 0U || viewport_height == 0U) {
    return error::invalid_argument;
  }
  {
    render_snapshot snapshot;
    auto selected = select_render_camera(owner, viewport_width, viewport_height, camera);
    if (!selected)
      return selected.error();
    if (*selected) {
      snapshot.camera = **selected;
      snapshot.has_camera = true;
    }
    world.each<mesh_renderer_component>(
        [&](entt::entity native_entity, const mesh_renderer_component& component) {
          if (!component.value.mesh || !component.value.material)
            return;
          const auto entity = world.encode_entity(native_entity);
          if (auto transform = world.scene().get_world(entity_id{entity})) {
            snapshot.instances.push_back({.mesh = component.value.mesh,
                                          .material = component.value.material,
                                          .transform = *transform});
          }
        });
    return snapshot;
  }
}

}
