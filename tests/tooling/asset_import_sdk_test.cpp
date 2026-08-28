// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_import_sdk.h"

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
  const auto fixture_root = std::filesystem::path{GNEISS_TEST_GLTF_ROOT};
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-import-sdk-test";
  const auto sources = root / "sources";
  const auto imported = root / "assets" / "imported";
  const auto first_source = sources / "models" / "first.gltf";
  const auto second_source = sources / "props" / "first.gltf";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(first_source.parent_path());
  std::filesystem::create_directories(second_source.parent_path());
  std::filesystem::copy_file(fixture_root / "static_triangle.gltf", first_source);
  std::filesystem::copy_file(fixture_root / "static_triangle.gltf", second_source);

  const auto first = asset_import::import_project_asset(
      {.source_root = sources, .imported_root = imported, .source_path = first_source});
  const auto repeated = asset_import::import_project_asset(
      {.source_root = sources, .imported_root = imported, .source_path = first_source});
  const auto second = asset_import::import_project_asset(
      {.source_root = sources, .imported_root = imported, .source_path = second_source});
  if (first.result != asset_import::import_asset_result::success ||
      repeated.result != asset_import::import_asset_result::success ||
      second.result != asset_import::import_asset_result::success || first.source_key.empty() ||
      first.source_key != repeated.source_key || first.source_key == second.source_key ||
      first.output_directory == second.output_directory || first.output_uris.empty()) {
    std::filesystem::remove_all(root);
    return 1;
  }

  const auto scene_path = first.output_directory / "scenes" / "scene.scene.json";
  const auto preserved_scene = read_file(scene_path);
  if (preserved_scene.find("asset://imported/" + first.source_key + "/models/") ==
      std::string::npos) {
    std::filesystem::remove_all(root);
    return 2;
  }
  std::filesystem::copy_file(fixture_root / "missing_normal.gltf", first_source,
                             std::filesystem::copy_options::overwrite_existing);
  const auto failed = asset_import::import_project_asset(
      {.source_root = sources, .imported_root = imported, .source_path = first_source});
  if (failed.result != asset_import::import_asset_result::unsupported_feature ||
      failed.source_key != first.source_key || read_file(scene_path) != preserved_scene) {
    std::filesystem::remove_all(root);
    return 3;
  }

  const auto escaped =
      asset_import::import_project_asset({.source_root = sources,
                                          .imported_root = imported,
                                          .source_path = fixture_root / "static_triangle.gltf"});
  const auto unsupported_source = sources / "readme.txt";
  {
    std::ofstream stream(unsupported_source);
    stream << "不是资产";
  }
  const auto unsupported = asset_import::import_project_asset(
      {.source_root = sources, .imported_root = imported, .source_path = unsupported_source});
  if (escaped.result != asset_import::import_asset_result::invalid_argument ||
      unsupported.result != asset_import::import_asset_result::unsupported_feature) {
    std::filesystem::remove_all(root);
    return 4;
  }

  std::filesystem::remove_all(root);
  return 0;
}
