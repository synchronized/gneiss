// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_index.hpp"

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
  namespace asset_import = gneiss::tooling;
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-index-test";
  const auto path = root / ".gneiss" / "asset-index.json";
  const auto source = root / "sources" / "model.gltf";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(source.parent_path());
  write_file(source, "source-content");

  std::string content_hash;
  if (gneiss::tooling::hash_source_file(source, content_hash).result !=
          gneiss::tooling::asset_index_result::success ||
      !content_hash.starts_with("fnv1a64:")) {
    return 1;
  }
  gneiss::tooling::asset_index index;
  gneiss::tooling::asset_index_entry entry{
      .source_path = "model.gltf",
      .source_key = "0123456789abcdef",
      .importer_id = "gneiss.gltf",
      .importer_version = gneiss::tooling::gltf_importer_version,
      .content_hash = content_hash,
      .state = gneiss::tooling::asset_import_state::ready,
      .output_uris = {"asset://imported/0123456789abcdef/scenes/scene.scene.json"}};
  if (gneiss::tooling::upsert_asset_index_entry(index, entry).result !=
          gneiss::tooling::asset_index_result::success ||
      gneiss::tooling::save_asset_index(path, index).result !=
          gneiss::tooling::asset_index_result::success) {
    std::filesystem::remove_all(root);
    return 2;
  }
  const auto first_json = read_file(path);
  gneiss::tooling::asset_index loaded;
  if (gneiss::tooling::load_asset_index(path, loaded).result !=
          gneiss::tooling::asset_index_result::success ||
      loaded.entries.size() != 1U || loaded.entries[0].content_hash != content_hash ||
      gneiss::tooling::save_asset_index(path, loaded).result !=
          gneiss::tooling::asset_index_result::success ||
      read_file(path) != first_json) {
    std::filesystem::remove_all(root);
    return 3;
  }

  auto replacement = entry;
  replacement.state = gneiss::tooling::asset_import_state::stale;
  if (gneiss::tooling::upsert_asset_index_entry(loaded, replacement).result !=
          gneiss::tooling::asset_index_result::success ||
      loaded.entries.size() != 1U ||
      loaded.entries[0].state != gneiss::tooling::asset_import_state::stale) {
    std::filesystem::remove_all(root);
    return 4;
  }
  auto escaped = entry;
  escaped.output_uris = {"asset://imported/0123456789abcdef/../escape"};
  auto collision = entry;
  collision.source_path = "other.gltf";
  if (gneiss::tooling::upsert_asset_index_entry(loaded, std::move(escaped)).result !=
          gneiss::tooling::asset_index_result::invalid_format ||
      gneiss::tooling::upsert_asset_index_entry(loaded, std::move(collision)).result !=
          gneiss::tooling::asset_index_result::invalid_format) {
    std::filesystem::remove_all(root);
    return 5;
  }

  write_file(path, R"({"format":"gneiss.asset-index","version":2,"entries":[]})");
  if (gneiss::tooling::load_asset_index(path, loaded).result !=
      gneiss::tooling::asset_index_result::unsupported_version) {
    std::filesystem::remove_all(root);
    return 6;
  }
  write_file(
      path,
      R"({"format":"gneiss.asset-index","version":1,"entries":[{"source":"../bad.gltf","source_key":"key","importer":"gneiss.gltf","importer_version":1,"hash":"x","state":"ready","outputs":[]}]})");
  if (gneiss::tooling::load_asset_index(path, loaded).result !=
      gneiss::tooling::asset_index_result::invalid_format) {
    std::filesystem::remove_all(root);
    return 7;
  }
  write_file(path, first_json);
  if (gneiss::tooling::load_asset_index(path, loaded).result !=
          gneiss::tooling::asset_index_result::success ||
      std::filesystem::exists(path.string() + ".gneiss-staging") ||
      std::filesystem::exists(path.string() + ".gneiss-backup")) {
    std::filesystem::remove_all(root);
    return 8;
  }

  std::filesystem::remove_all(root);
  return 0;
}
