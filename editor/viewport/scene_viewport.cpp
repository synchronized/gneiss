// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "viewport/scene_viewport.hpp"
#include <gneiss/engine/function/scene/rendering/render_camera.hpp>
#include <gneiss/engine/function/scene/world.hpp>
namespace gneiss::editor {
error scene_viewport::resize(render::service& renderer, render::target_description size) {
  if (retired_target_) {
    auto retired = renderer.destroy_target(retired_target_);
    if (!retired)
      return retired.error() == error::not_ready ? error::success : retired.error();
    retired_target_ = {};
  }
  if (target_ && extent_.width == size.width && extent_.height == size.height)
    return error::success;
  auto target = renderer.create_target(size);
  if (!target)
    return target.error();
  retired_target_ = target_;
  target_ = *target;
  extent_ = size;
  picking_.cancel();
  return error::success;
}
error scene_viewport::close(render::service& renderer) {
  reset_interaction();
  camera_.shutdown();
  for (auto* target : {&retired_target_, &target_}) {
    if (!*target)
      continue;
    auto removed = renderer.destroy_target(*target);
    if (!removed)
      return removed.error();
    *target = {};
  }
  extent_ = {};
  return error::success;
}
void scene_viewport::reset_interaction() {
  picking_.cancel();
  drag_ = {};
  wait_release_ = false;
  reset_transform_gizmo();
}
error scene_viewport::finish_interaction(document_editing& editing, bool mouse_down) {
  picking_.cancel();
  if (!drag_.is_active())
    return error::success;
  auto status = drag_.finish(editing.document(), editing.history());
  wait_release_ = mouse_down;
  reset_transform_gizmo();
  return status;
}
bool scene_viewport::draw_gizmo(document_editing& editing, error& status, const ImVec2& minimum,
                                const ImVec2& size) {
  const auto active = draw_transform_gizmo({.session = editing.document(),
                                            .history = editing.history(),
                                            .drag = drag_,
                                            .camera = camera_.current_transform(),
                                            .operation = operation_,
                                            .wait_release = wait_release_,
                                            .error = status},
                                           minimum, size);
  if (active || wait_release_)
    picking_.cancel();
  return active;
}
void scene_viewport::select_at(scene_edit_session& document, math::vector2 uv, bool gizmo_active) {
  if (gizmo_active || wait_release_) {
    picking_.cancel();
    return;
  }
  picking_.queue(document, target_, extent_, uv);
}
error scene_viewport::update_camera(const editor_camera_input& input, bool focus,
                                    document_editing& editing, world& source) {
  if (focus) {
    entity_id selected;
    if (auto node = editing.document().selected_node(); node)
      selected = node->node;
    else if (auto prefab = editing.document().selected_prefab_node(); prefab)
      selected = prefab->node;
    if (selected.is_valid()) {
      auto object = source.object(selected);
      if (!object)
        return object.error();
      return camera_.focus(object->world_transform());
    }
  }
  return camera_.update(input);
}
result<std::optional<render::view>> scene_viewport::view(world& source, rid scene,
                                                         render::target_description size) const {
  const auto settings = camera_.settings();
  const view_camera eye{.camera = {settings.fov, settings.near_plane, settings.far_plane},
                        .transform = camera_.current_transform()};
  auto selected = select_render_camera(source, size.width, size.height, &eye);
  if (!selected)
    return selected.error();
  if (!*selected)
    return std::optional<render::view>{};
  return std::optional<render::view>{{.scene = scene,
                                      .target = target_,
                                      .view_matrix = (**selected).view,
                                      .projection_matrix = (**selected).projection,
                                      .camera_position = (**selected).transform.translation}};
}
error scene_viewport::synchronize(render::service& renderer, const world_render_binding& binding,
                                  document_editing& editing, world& source) {
  if (!extent_.width || !extent_.height) {
    picking_.cancel();
    return error::success;
  }
  auto current = view(source, binding.scene(), extent_);
  if (!current) {
    picking_.cancel();
    return current.error();
  }
  if (!*current) {
    picking_.cancel();
    return error::success;
  }
  return picking_.update(renderer, binding, editing.document(), **current, extent_);
}
}
