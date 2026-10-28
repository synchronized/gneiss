// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "commands/author_property_edit.hpp"
#include "engine/core/error.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include "engine/core/error.hpp"
#include "engine/core/meta/type.hpp"
#include "engine/core/rid.hpp"

#include <algorithm>
#include <cmath>

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::engine::world* world_handle = nullptr;
  scene_document session;
  property_inspector_model inspector;
  editor_command_history history;
  scene_node_id first;
  scene_node_id second;
  gneiss::runtime::application_configuration desc;
  if (gneiss::engine::error{gneiss::runtime::application::create(desc, app)}.failed() ||
      (world_handle = app->native_world()) == nullptr ||
      session.create_empty(*app, world_handle).failed() ||
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
    gneiss::engine::math::transform local{};
    return app->native_world()->entity_get_local_transform(entity, local) ==
               gneiss::engine::error::success &&
           std::abs(local.translation[0] - expected) < 0.0001F;
  };
  const auto edit_x = [&](float next, editor_command_history& target, std::uint64_t serial) {
    const auto& component = inspector.components().front();
    const auto found = std::ranges::find(
        component.properties, gneiss::engine::transform_translation_field, &inspector_property::id);
    if (found == component.properties.end()) {
      return gneiss::engine::error{gneiss::engine::error::not_found};
    }
    auto value = found->value;
    std::get<gneiss::engine::math::vector3>(value.payload).x = next;
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
  if (edit_x(8.0F, no_capacity, 3U) != gneiss::engine::error::invalid_argument || !is_x(6.0F) ||
      no_capacity.can_undo()) {
    return 6;
  }
  const auto& component = inspector.components().front();
  const auto scale = std::ranges::find(component.properties, gneiss::engine::transform_scale_field,
                                       &inspector_property::id);
  if (scale == component.properties.end()) {
    return 7;
  }
  auto invalid = scale->value;
  std::get<gneiss::engine::math::vector3>(invalid.payload).x = 0.0F;
  if (edit_author_property(session, history, inspector, world_handle, component, *scale, invalid,
                           4U) != gneiss::engine::error::invalid_argument ||
      history.size() != 2U) {
    return 8;
  }
  scene_subtree_snapshot removed;
  if (session.destroy_subtree(first, removed).failed() ||
      history.undo() != gneiss::engine::error::not_found || !history.can_undo()) {
    return 9;
  }
  history.clear();
  session.close();
  return 0;
} catch (...) {
  return 10;
}
