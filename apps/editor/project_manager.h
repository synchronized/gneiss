// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PROJECT_MANAGER_H_
#define GNEISS_APPS_EDITOR_PROJECT_MANAGER_H_

#include "editor_project.h"

namespace gneiss::editor {

/** 运行独立工程选择窗口；关闭窗口但未选择工程时返回 not_ready。 */
[[nodiscard]] result run_project_manager(bool smoke, editor_project& output) noexcept;

} // namespace gneiss::editor

#endif
