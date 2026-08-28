// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "tooling/asset_import/import_ir.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gneiss::tooling::asset_import {

enum class import_asset_result {
  success,
  invalid_argument,
  source_unavailable,
  invalid_source,
  unsupported_feature,
  write_failed,
  index_update_failed,
};

struct import_asset_request {
  std::filesystem::path source_root;
  std::filesystem::path imported_root;
  std::filesystem::path source_path;
};

struct import_asset_report {
  import_asset_result result{import_asset_result::invalid_argument};
  import_ir_summary summary{};
  std::string source_key;
  std::filesystem::path output_directory;
  std::vector<std::string> output_uris;
  std::string diagnostic;
};

/// 导入一个位于 source_root 内的 glTF/GLB，并事务式替换其独占派生目录。
/// 本接口仅供同一进程内的 Gneiss 工具与 Editor 使用，不属于 Runtime 公共 ABI。
[[nodiscard]] import_asset_report import_project_asset(const import_asset_request& request);

/// 导入资产并在产物提交成功后原子更新工程资产索引。
[[nodiscard]] import_asset_report
import_project_asset_and_update_index(const import_asset_request& request,
                                      const std::filesystem::path& index_path);

} // namespace gneiss::tooling::asset_import
