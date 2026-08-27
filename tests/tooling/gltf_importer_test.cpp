// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include <filesystem>

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset_import = gneiss::tooling::asset_import;
  const std::filesystem::path root{GNEISS_TEST_GLTF_ROOT};

  const auto valid = asset_import::inspect_gltf(root / "static_triangle.gltf");
  if (valid.result != asset_import::inspect_result::success || valid.summary.scene_count != 1U ||
      valid.summary.node_count != 1U || valid.summary.mesh_count != 1U ||
      valid.summary.primitive_count != 1U) {
    return 1;
  }

  const auto unsupported = asset_import::inspect_gltf(root / "missing_normal.gltf");
  if (unsupported.result != asset_import::inspect_result::unsupported_feature ||
      unsupported.diagnostic.empty()) {
    return 2;
  }

  const auto missing = asset_import::inspect_gltf(root / "missing.gltf");
  if (missing.result != asset_import::inspect_result::source_unavailable ||
      missing.diagnostic.empty()) {
    return 3;
  }

  const auto empty = asset_import::inspect_gltf({});
  if (empty.result != asset_import::inspect_result::invalid_argument) {
    return 4;
  }

  return 0;
}
