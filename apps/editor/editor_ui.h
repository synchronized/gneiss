// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_UI_H_
#define GNEISS_APPS_EDITOR_EDITOR_UI_H_

namespace gneiss::editor {

enum class toolbar_icon { run, pause, stop };

/** 创建当前帧的单窗口 Editor DockSpace。 */
void begin_editor_workspace() noexcept;

/** 绘制不持有业务状态的工具栏图标按钮。 */
[[nodiscard]] bool toolbar_icon_button(const char* id, toolbar_icon icon, const char* tooltip,
                                       bool enabled, bool active = false) noexcept;

} // namespace gneiss::editor

#endif
