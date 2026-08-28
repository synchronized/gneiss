// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_browser_model.h"

#include "tooling/asset_import/asset_index.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

void write_file(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset_import = gneiss::tooling::asset_import;
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-browser-model-test";
  const auto sources = root / "sources";
  const auto assets = root / "assets";
  const auto source = sources / "models" / "sample.gltf";
  const auto missing_source = "models/missing.glb";
  std::filesystem::remove_all(root);
  write_file(source, "source-v1");
  write_file(assets / "scenes" / "main.scene.json", "{}");
  write_file(assets / "imported" / "first" / "models" / "mesh.gneiss-mesh", "mesh");
  write_file(assets / "imported" / "sample" / "scenes" / "scene.scene.json", "{}");

  std::string hash;
  if (asset_import::hash_source_file(source, hash).result !=
      asset_import::asset_index_result::success) {
    return 1;
  }
  asset_import::asset_index index{
      .entries = {{.source_path = "models/sample.gltf",
                   .source_key = "sample",
                   .importer_id = "gneiss.gltf",
                   .importer_version = 1U,
                   .content_hash = hash,
                   .state = asset_import::asset_import_state::ready,
                   .output_uris = {"asset://imported/sample/scenes/scene.scene.json"}},
                  {.source_path = missing_source,
                   .source_key = "missing",
                   .importer_id = "gneiss.gltf",
                   .importer_version = 1U,
                   .content_hash = "fnv1a64:0",
                   .state = asset_import::asset_import_state::ready,
                   .output_uris = {"asset://imported/missing/scenes/scene.scene.json"}}}};
  if (asset_import::save_asset_index(root / ".gneiss" / "asset-index.json", index).result !=
      asset_import::asset_index_result::success) {
    return 2;
  }

  gneiss::editor::asset_browser_model model;
  if (model.refresh(root, assets) != gneiss::editor::asset_browser_result::success ||
      model.entries().size() != 5U) {
    std::filesystem::remove_all(root);
    return 3;
  }
  const auto ready = std::ranges::find(model.entries(), "source:models/sample.gltf",
                                       &gneiss::editor::asset_browser_entry::id);
  const auto missing = std::ranges::find(model.entries(), "source:models/missing.glb",
                                         &gneiss::editor::asset_browser_entry::id);
  const auto authored = std::ranges::find(model.entries(), "asset:scenes/main.scene.json",
                                          &gneiss::editor::asset_browser_entry::id);
  const auto imported =
      std::ranges::find(model.entries(), "asset:imported/first/models/mesh.gneiss-mesh",
                        &gneiss::editor::asset_browser_entry::id);
  if (ready == model.entries().end() ||
      ready->status != gneiss::editor::asset_browser_status::ready ||
      missing == model.entries().end() ||
      missing->status != gneiss::editor::asset_browser_status::missing ||
      authored == model.entries().end() ||
      authored->kind != gneiss::editor::asset_browser_kind::authored_asset ||
      authored->asset_uri != "asset://scenes/main.scene.json" ||
      imported == model.entries().end() ||
      imported->kind != gneiss::editor::asset_browser_kind::imported_output ||
      !model.select(ready->id)) {
    std::filesystem::remove_all(root);
    return 4;
  }

  write_file(source, "source-v2");
  if (model.refresh(root, assets) != gneiss::editor::asset_browser_result::success) {
    std::filesystem::remove_all(root);
    return 5;
  }
  const auto stale = std::ranges::find(model.entries(), "source:models/sample.gltf",
                                       &gneiss::editor::asset_browser_entry::id);
  if (stale == model.entries().end() ||
      stale->status != gneiss::editor::asset_browser_status::stale ||
      model.selection() != stale->id) {
    std::filesystem::remove_all(root);
    return 6;
  }

  write_file(root / ".gneiss" / "asset-index.json", "invalid");
  const auto old_entries = model.entries().size();
  if (model.refresh(root, assets) != gneiss::editor::asset_browser_result::invalid_index ||
      model.entries().size() != old_entries || model.diagnostic().empty()) {
    std::filesystem::remove_all(root);
    return 7;
  }
  std::filesystem::remove_all(root);
  return 0;
}
