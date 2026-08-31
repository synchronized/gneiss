// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_PROJECT_H_
#define GNEISS_APPS_EDITOR_EDITOR_PROJECT_H_

#include <gneiss/app/project_description.h>

namespace gneiss::editor {

using editor_project = app::project_description;

/** 从工程根目录中加载并校验固定名称的 gneiss.project.json。 */
[[nodiscard]] inline result load_editor_project(const std::filesystem::path& project_root,
                                                editor_project& output) noexcept {
  return app::load_project_description(project_root, output);
}

} // namespace gneiss::editor

#endif
