// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_index.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void write_file(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset_import = gneiss::tooling::asset_import;
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-index-test";
  const auto path = root / ".gneiss" / "asset-index.json";
  const auto source = root / "sources" / "model.gltf";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(source.parent_path());
  write_file(source, "source-content");

  std::string content_hash;
  if (asset_import::hash_source_file(source, content_hash).result !=
          asset_import::asset_index_result::success ||
      !content_hash.starts_with("fnv1a64:")) {
    return 1;
  }
  asset_import::asset_index index;
  asset_import::asset_index_entry entry{
      .source_path = "model.gltf",
      .source_key = "0123456789abcdef",
      .importer_id = "gneiss.gltf",
      .importer_version = asset_import::gltf_importer_version,
      .content_hash = content_hash,
      .state = asset_import::asset_import_state::ready,
      .output_uris = {"asset://imported/0123456789abcdef/scenes/scene.scene.json"}};
  if (asset_import::upsert_asset_index_entry(index, entry).result !=
          asset_import::asset_index_result::success ||
      asset_import::save_asset_index(path, index).result !=
          asset_import::asset_index_result::success) {
    std::filesystem::remove_all(root);
    return 2;
  }
  const auto first_json = read_file(path);
  asset_import::asset_index loaded;
  if (asset_import::load_asset_index(path, loaded).result !=
          asset_import::asset_index_result::success ||
      loaded.entries.size() != 1U || loaded.entries[0].content_hash != content_hash ||
      asset_import::save_asset_index(path, loaded).result !=
          asset_import::asset_index_result::success ||
      read_file(path) != first_json) {
    std::filesystem::remove_all(root);
    return 3;
  }

  auto replacement = entry;
  replacement.state = asset_import::asset_import_state::stale;
  if (asset_import::upsert_asset_index_entry(loaded, replacement).result !=
          asset_import::asset_index_result::success ||
      loaded.entries.size() != 1U ||
      loaded.entries[0].state != asset_import::asset_import_state::stale) {
    std::filesystem::remove_all(root);
    return 4;
  }
  auto escaped = entry;
  escaped.output_uris = {"asset://imported/0123456789abcdef/../escape"};
  auto collision = entry;
  collision.source_path = "other.gltf";
  if (asset_import::upsert_asset_index_entry(loaded, std::move(escaped)).result !=
          asset_import::asset_index_result::invalid_format ||
      asset_import::upsert_asset_index_entry(loaded, std::move(collision)).result !=
          asset_import::asset_index_result::invalid_format) {
    std::filesystem::remove_all(root);
    return 5;
  }

  write_file(path, R"({"format":"gneiss.asset-index","version":2,"entries":[]})");
  if (asset_import::load_asset_index(path, loaded).result !=
      asset_import::asset_index_result::unsupported_version) {
    std::filesystem::remove_all(root);
    return 6;
  }
  write_file(
      path,
      R"({"format":"gneiss.asset-index","version":1,"entries":[{"source":"../bad.gltf","source_key":"key","importer":"gneiss.gltf","importer_version":1,"hash":"x","state":"ready","outputs":[]}]})");
  if (asset_import::load_asset_index(path, loaded).result !=
      asset_import::asset_index_result::invalid_format) {
    std::filesystem::remove_all(root);
    return 7;
  }
  write_file(path, first_json);
  if (asset_import::load_asset_index(path, loaded).result !=
          asset_import::asset_index_result::success ||
      std::filesystem::exists(path.string() + ".gneiss-staging") ||
      std::filesystem::exists(path.string() + ".gneiss-backup")) {
    std::filesystem::remove_all(root);
    return 8;
  }

  std::filesystem::remove_all(root);
  return 0;
}
