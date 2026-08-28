// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_NATIVE_DIALOG_H_
#define GNEISS_APPS_EDITOR_NATIVE_DIALOG_H_

#include <gneiss/core/result.hpp>

#include <filesystem>

namespace gneiss::editor {

/** 打开系统目录选择器；用户取消时返回 not_ready。 */
[[nodiscard]] result select_project_directory(std::filesystem::path& output) noexcept;

} // namespace gneiss::editor

#endif
