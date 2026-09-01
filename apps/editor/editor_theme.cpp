// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_theme.h"

#include <cmath>

namespace gneiss::editor {
namespace {

// 配色取自 Catppuccin Mocha，并按 Editor 语义重新映射。
// 来源：https://github.com/catppuccin/palette（MIT，Copyright (c) 2021 Catppuccin）。
[[nodiscard]] float linear_channel(int channel) noexcept {
  constexpr float channel_maximum = 255.0F;
  const auto srgb = static_cast<float>(channel) / channel_maximum;
  return srgb <= 0.04045F ? srgb / 12.92F : std::pow((srgb + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] ImVec4 rgb(int red, int green, int blue, float alpha = 1.0F) noexcept {
  return {linear_channel(red), linear_channel(green), linear_channel(blue), alpha};
}

const auto rosewater = rgb(245, 224, 220);
const auto mauve = rgb(203, 166, 247);
const auto red = rgb(243, 139, 168);
const auto peach = rgb(250, 179, 135);
const auto yellow = rgb(249, 226, 175);
const auto green = rgb(166, 227, 161);
const auto blue = rgb(137, 180, 250);
const auto text = rgb(205, 214, 244);
const auto subtext = rgb(166, 173, 200);
const auto overlay = rgb(108, 112, 134);
const auto surface2 = rgb(88, 91, 112);
const auto surface1 = rgb(69, 71, 90);
const auto surface0 = rgb(49, 50, 68);
const auto base = rgb(30, 30, 46);
const auto mantle = rgb(24, 24, 37);
const auto crust = rgb(17, 17, 27);

} // namespace

void apply_gneiss_mocha_theme() noexcept {
  auto& style = ImGui::GetStyle();
  style.WindowPadding = {12.0F, 12.0F};
  style.FramePadding = {9.0F, 5.0F};
  style.CellPadding = {8.0F, 5.0F};
  style.ItemSpacing = {8.0F, 7.0F};
  style.ItemInnerSpacing = {6.0F, 5.0F};
  style.ScrollbarSize = 13.0F;
  style.GrabMinSize = 10.0F;
  style.WindowBorderSize = 1.0F;
  style.ChildBorderSize = 1.0F;
  style.PopupBorderSize = 1.0F;
  style.FrameBorderSize = 0.0F;
  style.WindowRounding = 0.0F;
  style.ChildRounding = 7.0F;
  style.FrameRounding = 5.0F;
  style.PopupRounding = 6.0F;
  style.ScrollbarRounding = 7.0F;
  style.GrabRounding = 5.0F;
  style.TabRounding = 5.0F;
  style.DisabledAlpha = 0.55F;
  style.AntiAliasedLines = true;
  style.AntiAliasedLinesUseTex = true;
  style.AntiAliasedFill = true;
  style.CircleTessellationMaxError = 0.20F;

  auto* colors = style.Colors;
  colors[ImGuiCol_Text] = text;
  colors[ImGuiCol_TextDisabled] = subtext;
  colors[ImGuiCol_WindowBg] = base;
  colors[ImGuiCol_ChildBg] = surface0;
  colors[ImGuiCol_PopupBg] = mantle;
  colors[ImGuiCol_Border] = surface1;
  colors[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0.0F);
  colors[ImGuiCol_FrameBg] = surface0;
  colors[ImGuiCol_FrameBgHovered] = surface1;
  colors[ImGuiCol_FrameBgActive] = surface2;
  colors[ImGuiCol_TitleBg] = mantle;
  colors[ImGuiCol_TitleBgActive] = surface0;
  colors[ImGuiCol_TitleBgCollapsed] = mantle;
  colors[ImGuiCol_MenuBarBg] = mantle;
  colors[ImGuiCol_ScrollbarBg] = mantle;
  colors[ImGuiCol_ScrollbarGrab] = surface1;
  colors[ImGuiCol_ScrollbarGrabHovered] = surface2;
  colors[ImGuiCol_ScrollbarGrabActive] = overlay;
  colors[ImGuiCol_CheckMark] = peach;
  colors[ImGuiCol_SliderGrab] = peach;
  colors[ImGuiCol_SliderGrabActive] = mauve;
  colors[ImGuiCol_Button] = surface0;
  colors[ImGuiCol_ButtonHovered] = surface1;
  colors[ImGuiCol_ButtonActive] = surface2;
  colors[ImGuiCol_Header] = rgb(250, 179, 135, 0.26F);
  colors[ImGuiCol_HeaderHovered] = rgb(250, 179, 135, 0.38F);
  colors[ImGuiCol_HeaderActive] = rgb(250, 179, 135, 0.48F);
  colors[ImGuiCol_Separator] = surface1;
  colors[ImGuiCol_SeparatorHovered] = peach;
  colors[ImGuiCol_SeparatorActive] = mauve;
  colors[ImGuiCol_ResizeGrip] = rgb(250, 179, 135, 0.22F);
  colors[ImGuiCol_ResizeGripHovered] = rgb(250, 179, 135, 0.55F);
  colors[ImGuiCol_ResizeGripActive] = peach;
  colors[ImGuiCol_TabHovered] = surface1;
  colors[ImGuiCol_Tab] = mantle;
  colors[ImGuiCol_TabSelected] = surface0;
  colors[ImGuiCol_TabSelectedOverline] = peach;
  colors[ImGuiCol_TabDimmed] = crust;
  colors[ImGuiCol_TabDimmedSelected] = mantle;
  colors[ImGuiCol_TabDimmedSelectedOverline] = overlay;
  colors[ImGuiCol_PlotLines] = blue;
  colors[ImGuiCol_PlotLinesHovered] = peach;
  colors[ImGuiCol_PlotHistogram] = green;
  colors[ImGuiCol_PlotHistogramHovered] = yellow;
  colors[ImGuiCol_TableHeaderBg] = mantle;
  colors[ImGuiCol_TableBorderStrong] = surface1;
  colors[ImGuiCol_TableBorderLight] = surface0;
  colors[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0.0F);
  colors[ImGuiCol_TableRowBgAlt] = rgb(49, 50, 68, 0.42F);
  colors[ImGuiCol_TextLink] = blue;
  colors[ImGuiCol_TextSelectedBg] = rgb(203, 166, 247, 0.32F);
  colors[ImGuiCol_TreeLines] = surface2;
  colors[ImGuiCol_DragDropTarget] = yellow;
  colors[ImGuiCol_NavCursor] = peach;
  colors[ImGuiCol_NavWindowingHighlight] = rosewater;
  colors[ImGuiCol_NavWindowingDimBg] = rgb(17, 17, 27, 0.72F);
  colors[ImGuiCol_ModalWindowDimBg] = rgb(17, 17, 27, 0.78F);
}

ImVec4 theme_error_color() noexcept { return red; }
ImVec4 theme_warning_color() noexcept { return yellow; }
ImVec4 theme_success_color() noexcept { return green; }
ImVec4 theme_accent_color() noexcept { return peach; }

} // namespace gneiss::editor
