// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ui.h"

#include "editor_theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gneiss::editor {
namespace {

constexpr float hierarchy_width_ratio = 0.20F;
constexpr float inspector_width_ratio = 0.23F;
constexpr float console_height_ratio = 0.28F;
constexpr float assets_height_ratio = 0.40F;
constexpr std::string_view layout_header_v1 = "GNEISS_EDITOR_LAYOUT 1\n";
constexpr std::string_view layout_header_v2 = "GNEISS_EDITOR_LAYOUT 2\n";
constexpr std::string_view panel_prefix = "PANELS ";
constexpr std::size_t maximum_layout_size = 1024U * 1024U;

std::filesystem::path active_layout_path;
bool reset_layout_requested = false;

[[nodiscard]] std::string normalized_project_key(const std::filesystem::path& project_root) {
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(project_root, error);
  if (error) {
    normalized = project_root.lexically_normal();
  }
  const auto text = normalized.generic_u8string();
  std::string key(reinterpret_cast<const char*>(text.data()), text.size());
#if defined(_WIN32)
  std::ranges::transform(
      key, key.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
  return key;
}

[[nodiscard]] std::string stable_project_hash(std::string_view key) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const auto value : key) {
    hash ^= static_cast<unsigned char>(value);
    hash *= UINT64_C(1099511628211);
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] bool replace_file(const std::filesystem::path& source,
                                const std::filesystem::path& destination) {
#if defined(_WIN32)
  return MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return std::rename(source.c_str(), destination.c_str()) == 0;
#endif
}

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

result initialize_editor_layout(const std::filesystem::path& user_state_file,
                                const std::filesystem::path& project_root,
                                editor_panel_visibility& visibility) noexcept {
  try {
    visibility = {};
    active_layout_path = user_state_file.parent_path() / "layouts" /
                         (stable_project_hash(normalized_project_key(project_root)) + ".layout");
    reset_layout_requested = false;
    std::ifstream stream(active_layout_path, std::ios::binary);
    if (!stream) {
      return result::success;
    }
    const std::string contents{std::istreambuf_iterator<char>(stream),
                               std::istreambuf_iterator<char>()};
    if (contents.size() > maximum_layout_size) {
      return result::invalid_argument;
    }
    std::string_view ini;
    if (contents.starts_with(layout_header_v1)) {
      visibility = {};
      ini = std::string_view(contents).substr(layout_header_v1.size());
    } else if (contents.starts_with(layout_header_v2)) {
      auto remaining = std::string_view(contents).substr(layout_header_v2.size());
      const auto newline = remaining.find('\n');
      if (newline == std::string_view::npos) {
        return result::invalid_argument;
      }
      const auto panels = remaining.substr(0U, newline);
      if (!panels.starts_with(panel_prefix) || panels.size() != panel_prefix.size() + 5U) {
        return result::invalid_argument;
      }
      const auto flags = panels.substr(panel_prefix.size());
      if (!std::ranges::all_of(flags, [](char value) { return value == '0' || value == '1'; })) {
        return result::invalid_argument;
      }
      visibility = {.scene_hierarchy = flags[0] == '1',
                    .asset_browser = flags[1] == '1',
                    .scene_view = flags[2] == '1',
                    .inspector = flags[3] == '1',
                    .console = flags[4] == '1'};
      ini = remaining.substr(newline + 1U);
    } else {
      return result::invalid_argument;
    }
    if (ini.empty()) {
      return result::invalid_argument;
    }
    ImGui::LoadIniSettingsFromMemory(ini.data(), ini.size());
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result save_editor_layout(const editor_panel_visibility& visibility) noexcept {
  if (active_layout_path.empty()) {
    return result::invalid_state;
  }
  try {
    std::size_t size = 0U;
    const auto* data = ImGui::SaveIniSettingsToMemory(&size);
    if (data == nullptr || size == 0U ||
        size + layout_header_v2.size() + panel_prefix.size() + 6U > maximum_layout_size) {
      return result::invalid_state;
    }
    std::error_code error;
    std::filesystem::create_directories(active_layout_path.parent_path(), error);
    if (error) {
      return result::io;
    }
    auto temporary = active_layout_path;
    temporary += ".tmp";
    std::string contents(layout_header_v2);
    contents.append(panel_prefix);
    contents.push_back(visibility.scene_hierarchy ? '1' : '0');
    contents.push_back(visibility.asset_browser ? '1' : '0');
    contents.push_back(visibility.scene_view ? '1' : '0');
    contents.push_back(visibility.inspector ? '1' : '0');
    contents.push_back(visibility.console ? '1' : '0');
    contents.push_back('\n');
    contents.append(data, size);
    if (!write_text(temporary, contents)) {
      return result::io;
    }
    if (!replace_file(temporary, active_layout_path)) {
      std::filesystem::remove(temporary, error);
      return result::io;
    }
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

void reset_editor_layout() noexcept { reset_layout_requested = true; }

void begin_editor_workspace() noexcept {
  const auto* viewport = ImGui::GetMainViewport();
  const auto dockspace_id = ImHashStr("GneissEditorDockSpace");
  if (reset_layout_requested || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
    build_default_workspace(dockspace_id, *viewport);
    reset_layout_requested = false;
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
  const ImVec2 center{std::floor(position.x + (button_size.x * 0.5F)) + 0.5F,
                      std::floor(position.y + (button_size.y * 0.5F)) + 0.5F};
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
