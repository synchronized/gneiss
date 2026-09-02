// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_UI_H_
#define GNEISS_APPS_EDITOR_EDITOR_UI_H_

#include <gneiss/core/result.hpp>

#include <filesystem>

namespace gneiss::editor {

enum class toolbar_icon { run, pause, stop };

struct editor_panel_visibility final {
  bool scene_hierarchy = true;
  bool asset_browser = true;
  bool scene_view = true;
  bool inspector = true;
  bool console = true;
};

/** 从用户配置根加载当前工程的布局；无有效布局时使用默认工作区。 */
[[nodiscard]] result initialize_editor_layout(const std::filesystem::path& user_state_file,
                                              const std::filesystem::path& project_root,
                                              editor_panel_visibility& visibility) noexcept;

/** 原子保存当前工程布局；失败不会破坏上一份有效布局。 */
[[nodiscard]] result save_editor_layout(const editor_panel_visibility& visibility) noexcept;

/** 丢弃当前布局，并在本帧重建默认工作区。 */
void reset_editor_layout() noexcept;

/** 创建当前帧的单窗口 Editor DockSpace。 */
void begin_editor_workspace() noexcept;

/** 绘制不持有业务状态的工具栏图标按钮。 */
[[nodiscard]] bool toolbar_icon_button(const char* id, toolbar_icon icon, const char* tooltip,
                                       bool enabled, bool active = false) noexcept;

} // namespace gneiss::editor

#endif
