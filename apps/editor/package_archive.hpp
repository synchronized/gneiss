// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PACKAGE_ARCHIVE_H_
#define GNEISS_APPS_EDITOR_PACKAGE_ARCHIVE_H_

#include <gneiss/app/project_description.hpp>

#include <filesystem>
#include <string_view>

namespace gneiss::editor {

/** 为目录包生成版本化内容清单；清单自身不进入文件哈希列表。 */
[[nodiscard]] result write_package_manifest(const std::filesystem::path& package_root,
                                            const app::project_description& project,
                                            app::game_build_profile profile,
                                            std::string_view entrypoint) noexcept;

/** 校验清单声明的文件集合、大小与 SHA-256。 */
[[nodiscard]] result verify_package_manifest(const std::filesystem::path& package_root) noexcept;

/** 使用固定元数据和稳定路径顺序生成无压缩 ZIP。 */
[[nodiscard]] result write_deterministic_zip(const std::filesystem::path& package_root,
                                             const std::filesystem::path& archive) noexcept;

} // namespace gneiss::editor

#endif
