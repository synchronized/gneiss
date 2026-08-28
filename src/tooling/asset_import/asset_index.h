// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gneiss::tooling::asset_import {

inline constexpr std::uint32_t asset_index_version = 1U;
inline constexpr std::uint32_t gltf_importer_version = 1U;

enum class asset_index_result {
  success,
  not_found,
  invalid_format,
  unsupported_version,
  io_error,
};

enum class asset_import_state { ready, stale, missing };

struct asset_index_entry {
  std::string source_path;
  std::string source_key;
  std::string importer_id;
  std::uint32_t importer_version{};
  std::string content_hash;
  asset_import_state state{asset_import_state::ready};
  std::vector<std::string> output_uris;
};

struct asset_index {
  std::vector<asset_index_entry> entries;
};

struct asset_index_report {
  asset_index_result result{asset_index_result::invalid_format};
  std::string diagnostic;
};

[[nodiscard]] asset_index_report load_asset_index(const std::filesystem::path& path,
                                                  asset_index& output);
[[nodiscard]] asset_index_report save_asset_index(const std::filesystem::path& path,
                                                  const asset_index& index);
[[nodiscard]] asset_index_report hash_source_file(const std::filesystem::path& path,
                                                  std::string& output_hash);
[[nodiscard]] asset_index_report upsert_asset_index_entry(asset_index& index,
                                                          asset_index_entry entry);
/// 重新扫描 sources 中受支持的源资产，成功导入全部文件后替换可重建索引。
[[nodiscard]] asset_index_report rebuild_asset_index(const std::filesystem::path& source_root,
                                                     const std::filesystem::path& imported_root,
                                                     const std::filesystem::path& index_path);

} // namespace gneiss::tooling::asset_import
