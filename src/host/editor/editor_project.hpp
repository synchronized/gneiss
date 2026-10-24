// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_PROJECT_H_
#define GNEISS_APPS_EDITOR_EDITOR_PROJECT_H_

#include "engine/core/result.hpp"
#include <gneiss/app/project_description.hpp>

namespace gneiss::host {

using editor_project = gneiss::host::project_description;

/** 从工程根目录中加载并校验固定名称的 gneiss.project.json。 */
[[nodiscard]] inline gneiss::engine::result
load_editor_project(const std::filesystem::path& project_root, editor_project& output) noexcept {
  return gneiss::host::load_project_description(project_root, output);
}

} // namespace gneiss::host

#endif
