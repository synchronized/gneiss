// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_PROJECT_H_
#define GNEISS_APPS_EDITOR_EDITOR_PROJECT_H_

#include <gneiss/core/result.hpp>

#include <filesystem>
#include <string>

namespace gneiss::editor {

struct editor_project final {
  std::filesystem::path project_file;
  std::filesystem::path project_root;
  std::filesystem::path asset_root;
  std::string name;
  std::string startup_scene;
};

/** 从工程根目录中加载并校验固定名称的 gneiss.project.json。 */
[[nodiscard]] result load_editor_project(const std::filesystem::path& project_root,
                                         editor_project& output) noexcept;

} // namespace gneiss::editor

#endif
