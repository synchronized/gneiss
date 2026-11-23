// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_PACKAGE_ARCHIVE_H_
#define GNEISS_APPS_EDITOR_PACKAGE_ARCHIVE_H_

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/project/project_description.hpp>

#include <filesystem>
#include <string_view>

namespace gneiss::host {

/** 为目录包生成版本化内容清单；清单自身不进入文件哈希列表。 */
[[nodiscard]] gneiss::error
write_package_manifest(const std::filesystem::path& package_root,
                       const gneiss::host::project_description& project,
                       gneiss::host::project_build_profile profile, std::string_view entrypoint);

/** 校验清单声明的文件集合、大小与 SHA-256。 */
[[nodiscard]] gneiss::error
verify_package_manifest(const std::filesystem::path& package_root);

/** 使用固定元数据和稳定路径顺序生成无压缩 ZIP。 */
[[nodiscard]] gneiss::error
write_deterministic_zip(const std::filesystem::path& package_root,
                        const std::filesystem::path& archive);

} // namespace gneiss::host

#endif
