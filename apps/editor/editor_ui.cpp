// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ui.h"

#include "editor_theme.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace gneiss::editor {
namespace {

constexpr float hierarchy_width_ratio = 0.20F;
constexpr float inspector_width_ratio = 0.23F;
constexpr float console_height_ratio = 0.28F;
constexpr float assets_height_ratio = 0.40F;

void build_default_workspace(ImGuiID dockspace_id, const ImGuiViewport& viewport) noexcept {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, viewport.WorkSize);

  auto center = dockspace_id;
  const auto left =
      ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, hierarchy_width_ratio, nullptr, &center);
  const auto right =
      ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, inspector_width_ratio, nullptr, &center);
  const auto bottom =
      ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, console_height_ratio, nullptr, &center);
  ImGuiID hierarchy = 0U;
  const auto assets =
      ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, assets_height_ratio, nullptr, &hierarchy);

  ImGui::DockBuilderDockWindow("Scene Hierarchy", hierarchy);
  ImGui::DockBuilderDockWindow("Asset Browser", assets);
  ImGui::DockBuilderDockWindow("Scene View", center);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Console", bottom);
  ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace

void begin_editor_workspace() noexcept {
  const auto* viewport = ImGui::GetMainViewport();
  const auto dockspace_id = ImHashStr("GneissEditorDockSpace");
  if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
    build_default_workspace(dockspace_id, *viewport);
  }
  ImGui::DockSpaceOverViewport(dockspace_id, viewport);
}

bool toolbar_icon_button(const char* id, toolbar_icon icon, const char* tooltip, bool enabled,
                         bool active) noexcept {
  if (id == nullptr || tooltip == nullptr) {
    return false;
  }
  constexpr ImVec2 button_size{28.0F, 22.0F};
  const auto position = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, button_size);
  const auto hovered = enabled && ImGui::IsItemHovered();
  const auto held = enabled && ImGui::IsItemActive();
  const auto clicked = enabled && ImGui::IsItemClicked();
  const auto background = held      ? ImGuiCol_ButtonActive
                          : hovered ? ImGuiCol_ButtonHovered
                                    : ImGuiCol_Button;
  auto* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(position, ImVec2(position.x + button_size.x, position.y + button_size.y),
                           ImGui::GetColorU32(background), 4.0F);

  auto icon_color = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  if (active && enabled) {
    icon_color = ImGui::ColorConvertFloat4ToU32(theme_success_color());
  }
  const ImVec2 center{position.x + (button_size.x * 0.5F), position.y + (button_size.y * 0.5F)};
  switch (icon) {
  case toolbar_icon::run:
    draw_list->AddTriangleFilled(ImVec2(center.x - 3.5F, center.y - 6.0F),
                                 ImVec2(center.x - 3.5F, center.y + 6.0F),
                                 ImVec2(center.x + 6.0F, center.y), icon_color);
    break;
  case toolbar_icon::pause:
    draw_list->AddRectFilled(ImVec2(center.x - 5.0F, center.y - 6.0F),
                             ImVec2(center.x - 1.5F, center.y + 6.0F), icon_color, 1.0F);
    draw_list->AddRectFilled(ImVec2(center.x + 1.5F, center.y - 6.0F),
                             ImVec2(center.x + 5.0F, center.y + 6.0F), icon_color, 1.0F);
    break;
  case toolbar_icon::stop:
    draw_list->AddRectFilled(ImVec2(center.x - 5.5F, center.y - 5.5F),
                             ImVec2(center.x + 5.5F, center.y + 5.5F), icon_color, 1.5F);
    break;
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", tooltip);
  }
  return clicked;
}

} // namespace gneiss::editor
