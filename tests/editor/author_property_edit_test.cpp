// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "author_property_edit.hpp"

#include <gneiss/engine/application.hpp>
#include <gneiss/engine/world.hpp>

#include <algorithm>
#include <cmath>

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  application app;
  gneiss_world world_handle = GNEISS_NULL_WORLD;
  editor_session session;
  property_inspector_model inspector;
  editor_command_history history;
  scene_node_id first;
  scene_node_id second;
  const gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  if (application::create_native(desc, app).failed() || app.get_world(world_handle).failed() ||
      session.create_empty(app.get(), world_handle).failed() ||
      session.create_node("first", {}, first).failed() ||
      session.create_node("second", {}, second).failed() || session.select(first).failed() ||
      inspector.initialize().failed()) {
    return 1;
  }
  const auto entity = session.selected_node()->entity;
  if (inspector.refresh(world_handle, entity).failed()) {
    return 2;
  }
  const auto is_x = [&](float expected) {
    transform local{};
    return world_ref{world_handle}.get_local_transform(entity, local) == result::success &&
           std::abs(local.translation[0] - expected) < 0.0001F;
  };
  const auto edit_x = [&](float next, editor_command_history& target, std::uint64_t serial) {
    const auto& component = inspector.components().front();
    const auto found = std::ranges::find(component.properties, GNEISS_TRANSFORM_FIELD_TRANSLATION,
                                         &inspector_property::id);
    if (found == component.properties.end()) {
      return result{result::not_found};
    }
    auto value = found->value;
    value.payload.vec3_value.x = next;
    return edit_author_property(session, target, inspector, world_handle, component, *found, value,
                                serial);
  };
  // 同一次拖拽合并，新的交互形成独立历史；临时输入不被命令借用。
  if (edit_x(2.0F, history, 1U).failed() || edit_x(4.0F, history, 1U).failed() ||
      history.size() != 1U || !is_x(4.0F) || edit_x(6.0F, history, 2U).failed() ||
      history.size() != 2U || !is_x(6.0F)) {
    return 3;
  }
  // 选择改变后，撤销/重做必须仍定位原作者 UUID。
  if (session.select(second).failed() ||
      inspector.refresh(world_handle, session.selected_node()->entity).failed() ||
      history.undo().failed() || !is_x(4.0F) || history.undo().failed() || !is_x(0.0F) ||
      history.redo().failed() || !is_x(4.0F) || history.redo().failed() || !is_x(6.0F)) {
    return 4;
  }
  if (session.select(first).failed() || inspector.refresh(world_handle, entity).failed()) {
    return 5;
  }
  editor_command_history no_capacity{0U};
  if (edit_x(8.0F, no_capacity, 3U) != result::invalid_argument || !is_x(6.0F) ||
      no_capacity.can_undo()) {
    return 6;
  }
  const auto& component = inspector.components().front();
  const auto scale = std::ranges::find(component.properties, GNEISS_TRANSFORM_FIELD_SCALE,
                                       &inspector_property::id);
  if (scale == component.properties.end()) {
    return 7;
  }
  auto invalid = scale->value;
  invalid.payload.vec3_value.x = 0.0F;
  if (edit_author_property(session, history, inspector, world_handle, component, *scale, invalid,
                           4U) != result::invalid_argument ||
      history.size() != 2U) {
    return 8;
  }
  scene_subtree_snapshot removed;
  if (session.destroy_subtree(first, removed).failed() || history.undo() != result::not_found ||
      !history.can_undo()) {
    return 9;
  }
  history.clear();
  session.close();
  return 0;
} catch (...) {
  return 10;
}
