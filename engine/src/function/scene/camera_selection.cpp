// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include "function/scene/detail/camera_selection_state.hpp"
#include "function/scene/detail/world_component_access.hpp"
namespace gneiss {
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

} // namespace gneiss
