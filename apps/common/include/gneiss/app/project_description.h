// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_COMMON_PROJECT_DESCRIPTION_H_
#define GNEISS_APPS_COMMON_PROJECT_DESCRIPTION_H_

#include <gneiss/core/result.hpp>

#include <filesystem>
#include <string>

namespace gneiss::app {

/** 经过校验的工程运行描述；路径均为规范绝对路径。 */
struct project_description final {
  std::filesystem::path project_file;
  std::filesystem::path project_root;
  std::filesystem::path asset_root;
  std::string name;
  std::string startup_scene;
};

/** 从工程根目录加载并校验固定名称的 gneiss.project.json。 */
[[nodiscard]] result load_project_description(const std::filesystem::path& project_root,
                                              project_description& output) noexcept;

} // namespace gneiss::app

#endif
