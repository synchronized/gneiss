// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PROJECT_WORKSPACE_H_
#define GNEISS_APPS_EDITOR_PROJECT_WORKSPACE_H_

#include "editor_project.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace gneiss::editor {

/** 返回 Editor 用户状态文件的默认路径。 */
[[nodiscard]] std::filesystem::path default_editor_state_path();

/** 加载并校验最近工程；失效工程会被忽略。 */
[[nodiscard]] result load_recent_projects(const std::filesystem::path& state_file,
                                          std::vector<editor_project>& output) noexcept;

/** 将工程移动到最近工程列表首位，最多保留十项。 */
[[nodiscard]] result remember_recent_project(const std::filesystem::path& state_file,
                                             const editor_project& project) noexcept;

/** 在不存在的目录中创建可立即打开的最小工程。 */
[[nodiscard]] result create_editor_project(const std::filesystem::path& project_root,
                                           std::string_view name, editor_project& output) noexcept;

} // namespace gneiss::editor

#endif
