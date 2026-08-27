// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_writer.h"
#include "tooling/asset_import/gltf_importer.h"

#include <array>
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
  const auto source = std::filesystem::path{GNEISS_TEST_GLTF_ROOT} / "static_triangle.gltf";
  const auto imported = asset_import::inspect_gltf(source);
  if (imported.result != asset_import::inspect_result::success) {
    return 1;
  }
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-writer-test";
  const auto first = root / "first";
  const auto second = root / "second";
  std::filesystem::remove_all(root);
  if (!asset_import::write_assets(imported.data, first).success ||
      !asset_import::write_assets(imported.data, second).success) {
    return 2;
  }
  constexpr std::array files = {"models/mesh-0.mesh.json", "materials/material-0.material.json",
                                "textures/image-0.texture.json", "textures/image-0.png",
                                "scenes/scene.scene.json"};
  for (const auto* file : files) {
    if (read_file(first / file) != read_file(second / file) || read_file(first / file).empty()) {
      return 3;
    }
  }
  std::filesystem::remove_all(root);
  return 0;
}
