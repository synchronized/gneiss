// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_NATIVE_DIALOG_H_
#define GNEISS_APPS_EDITOR_NATIVE_DIALOG_H_

#include <gneiss/core/result.hpp>

#include <filesystem>

namespace gneiss::editor {

/** 打开系统目录选择器；用户取消时返回 not_ready。 */
[[nodiscard]] result select_project_directory(std::filesystem::path& output) noexcept;

/** 打开系统文件选择器并限定 glTF/GLB；用户取消时返回 not_ready。 */
[[nodiscard]] result select_source_asset(std::filesystem::path& output) noexcept;

/** 选择现有场景 JSON；用户取消时返回 not_ready。 */
[[nodiscard]] result select_scene_file(std::filesystem::path& output) noexcept;

/** 选择新场景 JSON 保存路径；用户取消时返回 not_ready。 */
[[nodiscard]] result select_scene_save_path(std::filesystem::path& output) noexcept;

} // namespace gneiss::editor

#endif
