// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_view.hpp"

#include <imgui.h>

// ImGuizmo 的头依赖已定义的 ImGui 类型。
#include <ImGuizmo.h>

namespace gneiss::editor {
namespace {
ImGuizmo::OPERATION to_imguizmo_operation(transform_gizmo_operation operation) noexcept {
  switch (operation) {
  case transform_gizmo_operation::translate:
    return ImGuizmo::TRANSLATE;
  case transform_gizmo_operation::rotate:
    return ImGuizmo::ROTATE;
  case transform_gizmo_operation::scale:
    return ImGuizmo::SCALE;
  }
  return ImGuizmo::SCALE;
}
} // namespace

void begin_transform_gizmo_frame() noexcept { ImGuizmo::BeginFrame(); }
void reset_transform_gizmo() noexcept {
  ImGuizmo::Enable(false);
  ImGuizmo::Enable(true);
}

bool draw_transform_gizmo(const transform_gizmo_context& context, const ImVec2& minimum,
                          const ImVec2& size) noexcept {
  const auto* selected = context.session.selected_node();
  const auto* prefab = context.session.selected_prefab_node();
  const auto editable_prefab = prefab != nullptr && !prefab->is_instance_root;
  if ((selected == nullptr && !editable_prefab) || size.x <= 1.0F || size.y <= 1.0F) {
    if (context.drag.is_active()) {
      context.error = context.drag.finish(context.session, context.history);
      context.wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      reset_transform_gizmo();
    }
    return false;
  }

  if (context.drag.is_active() && !context.drag.matches_selection(context.session)) {
    context.error = context.drag.finish(context.session, context.history);
    context.wait_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    reset_transform_gizmo();
    return true;
  }
  if (context.wait_release) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      return true;
    }
    context.wait_release = false;
  }
  const auto node = selected != nullptr ? selected->node : prefab->node;

  gneiss::transform world{};
  auto operation = context.world.get_world_transform(node, world);
  gneiss::editor::gizmo_matrix model{};
  if (operation == gneiss::result::success) {
    operation = gneiss::editor::transform_to_gizmo_matrix(world, model);
  }
  if (operation != gneiss::result::success) {
    context.error = operation;
    return false;
  }

  const auto* viewport = ImGui::GetMainViewport();
  if (viewport == nullptr || viewport->Size.x <= 1.0F || viewport->Size.y <= 1.0F) {
    return false;
  }
  auto view = gneiss::editor::build_gizmo_view_matrix(context.camera);
  auto projection =
      gneiss::editor::build_gizmo_projection_matrix(viewport->Size.x / viewport->Size.y);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
  ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);
  ImGui::GetWindowDrawList()->AddText(
      ImVec2(minimum.x + 8.0F, minimum.y + size.y - ImGui::GetTextLineHeight() - 8.0F),
      IM_COL32(205, 214, 244, 210), "Grid: 1 unit = 1 m | adaptive spacing");
  const auto native_operation = to_imguizmo_operation(context.operation);
  const auto manipulated = ImGuizmo::Manipulate(view.data(), projection.data(), native_operation,
                                                ImGuizmo::WORLD, model.data());
  const auto using_now = ImGuizmo::IsUsing();
  if (using_now && !context.drag.is_active()) {
    context.error = context.drag.begin(context.session);
  }
  if (manipulated && context.drag.matches_selection(context.session)) {
    context.error = context.drag.preview(context.session, context.world.get(), model);
  }
  if (!using_now && context.drag.is_active()) {
    context.error = context.drag.finish(context.session, context.history);
  }
  return using_now || ImGuizmo::IsOver(native_operation);
}

} // namespace gneiss::editor
