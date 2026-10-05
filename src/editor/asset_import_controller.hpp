// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "tooling/asset_import/asset_import_sdk.hpp"

#include <filesystem>
#include <string>

namespace gneiss::editor {

enum class editor_import_result {
  success,
  invalid_argument,
  unsupported,
  io_error,
  import_failed,
};

struct editor_import_report {
  editor_import_result result{editor_import_result::invalid_argument};
  std::filesystem::path source_path;
  gneiss::tooling::asset_import::import_asset_report import;
  std::string diagnostic;
};

/** 将外部 glTF/GLB 安全复制到工程 sources 后导入并更新索引。 */
[[nodiscard]] editor_import_report
import_external_asset(const std::filesystem::path& project_root,
                      const std::filesystem::path& asset_root,
                      const std::filesystem::path& external_source);

/** 重新导入已经位于工程 sources 中的源资产。 */
[[nodiscard]] editor_import_report reimport_source_asset(const std::filesystem::path& project_root,
                                                         const std::filesystem::path& asset_root,
                                                         const std::filesystem::path& source_path);

/// 后台调用的受控入口；回调与导入在同一工作线程执行。
[[nodiscard]] editor_import_report reimport_source_asset_controlled(
    const std::filesystem::path& project, const std::filesystem::path& assets,
    const std::filesystem::path& source, const tooling::asset_import::import_control& control);
[[nodiscard]] editor_import_report import_external_asset_controlled(
    const std::filesystem::path& project, const std::filesystem::path& assets,
    const std::filesystem::path& source, const tooling::asset_import::import_control& control);

} // namespace gneiss::editor
