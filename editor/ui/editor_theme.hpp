// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_THEME_H_
#define GNEISS_APPS_EDITOR_EDITOR_THEME_H_

#include <imgui.h>

namespace gneiss::editor {

/** 应用以 Catppuccin Mocha 为基础、Peach 为主强调色的 Editor 主题。 */
void apply_gneiss_mocha_theme() noexcept;

[[nodiscard]] ImVec4 theme_error_color() noexcept;
[[nodiscard]] ImVec4 theme_warning_color() noexcept;
[[nodiscard]] ImVec4 theme_success_color() noexcept;
[[nodiscard]] ImVec4 theme_accent_color() noexcept;

} // namespace gneiss::editor

#endif
