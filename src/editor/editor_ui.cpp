// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ui.hpp"
#include "engine/core/result.hpp"

#include "editor_theme.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <system_error>

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

bool reset_layout_requested = false;
std::uint64_t reset_generation{};

[[nodiscard]] std::string normalized_project_key(std::string_view project_key) {
  std::string key(project_key);
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

engine::result editor_layout::initialize(std::shared_ptr<gneiss::engine::files::service> files,
                                         std::string_view project_key,
                                         editor_panel_visibility& visibility) noexcept try {
  if (!files || project_key.empty() || saving())
    return engine::result::invalid_argument;
  visibility = {};
  write_.reset();
  files_ = std::move(files);
  uri_ = "user://" + stable_project_hash(normalized_project_key(project_key)) + ".layout";
  reset_layout_requested = true;
  reset_generation_ = reset_generation;
  auto receipt = std::make_shared<read_receipt>();
  gneiss::engine::files::request_id request;
  const auto status = files_->read(
      uri_, {maximum_layout_size},
      [receipt](auto value) {
        receipt->value = std::move(value);
        receipt->complete = true;
      },
      request);
  read_ = status.ok() ? std::move(receipt) : nullptr;
  return status;
} catch (const std::bad_alloc&) {
  return engine::result::out_of_memory;
} catch (...) {
  return engine::result::internal;
}

engine::result editor_layout::advance(editor_panel_visibility& visibility) noexcept {
  if (!read_)
    return engine::result::success;
  if (!read_->complete)
    return engine::result::not_ready;
  auto receipt = std::move(read_);
  if (reset_generation_ != reset_generation)
    return engine::result::success;
  if (receipt->value.status == engine::result::not_found)
    return engine::result::success;
  if (receipt->value.status.failed())
    return receipt->value.status;
  try {
    const std::string_view contents(reinterpret_cast<const char*>(receipt->value.data.data()),
                                    receipt->value.data.size());
    editor_panel_visibility loaded;
    std::string_view ini;
    if (contents.starts_with(layout_header_v1)) {
      loaded = {};
      ini = std::string_view(contents).substr(layout_header_v1.size());
    } else if (contents.starts_with(layout_header_v2)) {
      auto remaining = std::string_view(contents).substr(layout_header_v2.size());
      const auto newline = remaining.find('\n');
      if (newline == std::string_view::npos) {
        return gneiss::engine::result::invalid_argument;
      }
      const auto panels = remaining.substr(0U, newline);
      if (!panels.starts_with(panel_prefix) || panels.size() != panel_prefix.size() + 5U) {
        return gneiss::engine::result::invalid_argument;
      }
      const auto flags = panels.substr(panel_prefix.size());
      if (!std::ranges::all_of(flags, [](char value) { return value == '0' || value == '1'; })) {
        return gneiss::engine::result::invalid_argument;
      }
      loaded = {.scene_hierarchy = flags[0] == '1',
                .asset_browser = flags[1] == '1',
                .scene_view = flags[2] == '1',
                .inspector = flags[3] == '1',
                .console = flags[4] == '1'};
      ini = remaining.substr(newline + 1U);
    } else {
      return gneiss::engine::result::invalid_argument;
    }
    if (ini.empty()) {
      return gneiss::engine::result::invalid_argument;
    }
    visibility = loaded;
    reset_layout_requested = false;
    ImGui::LoadIniSettingsFromMemory(ini.data(), ini.size());
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::io;
  }
}

engine::result editor_layout::save(const editor_panel_visibility& visibility) noexcept {
  if (!files_ || uri_.empty())
    return engine::result::invalid_state;
  if (saving())
    return engine::result::not_ready;
  // 退出保存优先，迟到读取不能再覆盖当前用户布局。
  read_.reset();
  try {
    std::size_t size = 0U;
    const auto* data = ImGui::SaveIniSettingsToMemory(&size);
    if (data == nullptr || size == 0U ||
        size + layout_header_v2.size() + panel_prefix.size() + 6U > maximum_layout_size) {
      return gneiss::engine::result::invalid_state;
    }
    std::string contents(layout_header_v2);
    contents.append(panel_prefix);
    contents.push_back(visibility.scene_hierarchy ? '1' : '0');
    contents.push_back(visibility.asset_browser ? '1' : '0');
    contents.push_back(visibility.scene_view ? '1' : '0');
    contents.push_back(visibility.inspector ? '1' : '0');
    contents.push_back(visibility.console ? '1' : '0');
    contents.push_back('\n');
    contents.append(data, size);
    auto receipt = std::make_shared<write_receipt>();
    const auto bytes = std::as_bytes(std::span{contents.data(), contents.size()});
    gneiss::engine::files::request_id request;
    const auto status = files_->write(
        uri_, {bytes.begin(), bytes.end()}, {},
        [receipt](auto status) {
          receipt->status = status;
          receipt->complete = true;
        },
        request);
    write_ = status.ok() ? std::move(receipt) : nullptr;
    return status;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::io;
  }
}

engine::result editor_layout::save_result() const noexcept {
  return write_ && write_->complete ? write_->status : engine::result::not_ready;
}
void reset_editor_layout() noexcept {
  reset_layout_requested = true;
  ++reset_generation;
}

void begin_editor_workspace() noexcept {
  const auto* viewport = ImGui::GetMainViewport();
  const auto dockspace_id = ImHashStr("GneissEditorDockSpace");
  if (reset_layout_requested || ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
    build_default_workspace(dockspace_id, *viewport);
    reset_layout_requested = false;
  }
  // 3D 场景先于 ImGui 绘制，中央 Scene View 必须透过 DockSpace 宿主窗口显示场景。
  // 其他已停靠面板会在稍后使用正常的 WindowBg 独立绘制背景。
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
  ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::PopStyleColor();
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
