// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_import_controller.h"

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

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset_import = gneiss::tooling::asset_import;
  const auto fixtures = std::filesystem::path{GNEISS_EDITOR_TEST_GLTF_ROOT};
  const auto root = std::filesystem::temp_directory_path() / "gneiss-editor-asset-import-test";
  const auto assets = root / "assets";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(assets);

  const auto imported =
      gneiss::editor::import_external_asset(root, assets, fixtures / "static_triangle.gltf");
  if (imported.result != gneiss::editor::editor_import_result::success ||
      !std::filesystem::is_regular_file(imported.source_path) ||
      !std::filesystem::is_directory(imported.import.output_directory)) {
    std::filesystem::remove_all(root);
    return 1;
  }
  asset_import::asset_index index;
  const auto index_path = root / ".gneiss" / "asset-index.json";
  if (asset_import::load_asset_index(index_path, index).result !=
          asset_import::asset_index_result::success ||
      index.entries.size() != 1U) {
    std::filesystem::remove_all(root);
    return 2;
  }
  const auto old_index = read_file(index_path);
  std::filesystem::copy_file(fixtures / "missing_normal.gltf", imported.source_path,
                             std::filesystem::copy_options::overwrite_existing);
  const auto failed = gneiss::editor::reimport_source_asset(root, assets, imported.source_path);
  if (failed.result != gneiss::editor::editor_import_result::import_failed ||
      read_file(index_path) != old_index) {
    std::filesystem::remove_all(root);
    return 3;
  }
  std::filesystem::copy_file(fixtures / "static_triangle.gltf", imported.source_path,
                             std::filesystem::copy_options::overwrite_existing);
  const auto reimported = gneiss::editor::reimport_source_asset(root, assets, imported.source_path);
  if (reimported.result != gneiss::editor::editor_import_result::success) {
    std::filesystem::remove_all(root);
    return 4;
  }
  const auto duplicate =
      gneiss::editor::import_external_asset(root, assets, fixtures / "static_triangle.gltf");
  if (duplicate.result != gneiss::editor::editor_import_result::success ||
      duplicate.source_path == imported.source_path ||
      duplicate.source_path.filename() != "static_triangle-1.gltf") {
    std::filesystem::remove_all(root);
    return 5;
  }

  std::filesystem::remove_all(root);
  return 0;
}
