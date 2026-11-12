// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/rendering/render_camera.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
namespace gneiss {
result<std::optional<render_camera_snapshot>> select_render_camera(world& owner,
                                                                   std::uint32_t viewport_width,
                                                                   std::uint32_t viewport_height,
                                                                   const view_camera* camera) {
  auto state = detail::world_component_access::get(owner);
  if (!state)
    return state.error();
  if (!viewport_width || !viewport_height)
    return error::invalid_argument;
  auto& world = **state;
  view_camera selected;
  bool has_camera = camera != nullptr;
  if (camera != nullptr) {
    selected = *camera;
  } else {
    entity_id selected_entity;
    (void)get_active_camera(owner, selected_entity);
    const auto active_camera = selected_entity.get();
    if (const auto* component = world.get<camera_component>(active_camera); component != nullptr) {
      selected.camera = {component->settings().fov, component->settings().near,
                         component->settings().far};
      auto transform = owner.find(entity_id{active_camera}).world_transform();
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
    render_camera_snapshot snapshot{.camera = selected.camera,
                                    .transform = selected.transform,
                                    .view = view,
                                    .projection = projection,
                                    .viewport_width = viewport_width,
                                    .viewport_height = viewport_height};
    return std::optional<render_camera_snapshot>{snapshot};
  }
  return std::optional<render_camera_snapshot>{};
}
}
