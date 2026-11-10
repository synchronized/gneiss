// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/render_extraction.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/detail/camera_selection_state.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
namespace gneiss::engine {
error set_active_camera(world& owner, entity_id entity) {
  auto state = detail::world_component_access::get(owner);
  if (!state)
    return state.error();
  if (!(*state)->is_alive(entity.get()))
    return error::invalid_handle;
  if (!(*state)->get<camera_component>(entity.get()))
    return error::not_ready;
  (*state)->context<detail::camera_selection_state>().entity = entity;
  return error::success;
}
error get_active_camera(world& owner, entity_id& output) {
  output = {};
  auto state = detail::world_component_access::get(owner);
  if (!state)
    return state.error();
  auto& selection = (*state)->context<detail::camera_selection_state>();
  if (!(*state)->get<camera_component>(selection.entity.get())) {
    selection.entity = {};
    return error::not_ready;
  }
  output = selection.entity;
  return error::success;
}

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
    view_camera selected;
    bool has_camera = camera != nullptr;
    if (camera != nullptr) {
      selected = *camera;
    } else {
      entity_id selected_entity;
      (void)get_active_camera(owner, selected_entity);
      const auto active_camera = selected_entity.get();
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
          if (!component.value.mesh || !component.value.material)
            return;
          const auto entity = world.encode_entity(native_entity);
          if (auto transform = world.scene().get_world_for_entity(entity)) {
            snapshot.instances.push_back({.mesh = component.value.mesh,
                                          .material = component.value.material,
                                          .transform = *transform});
          }
        });
    return snapshot;
  }
}

}
