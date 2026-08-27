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
      valid.summary.primitive_count != 1U || valid.data.nodes.size() != 1U ||
      valid.data.nodes[0].name != "Triangle" || valid.data.nodes[0].mesh_index != 0U ||
      valid.data.meshes.size() != 1U || valid.data.meshes[0].primitives.size() != 1U ||
      valid.data.meshes[0].primitives[0].index_accessor != 3U) {
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

  const auto invalid_accessor = asset_import::inspect_gltf(root / "invalid_accessor.gltf");
  if (invalid_accessor.result != asset_import::inspect_result::invalid_source ||
      invalid_accessor.diagnostic.empty()) {
    return 4;
  }

  const auto line_primitive = asset_import::inspect_gltf(root / "line_primitive.gltf");
  if (line_primitive.result != asset_import::inspect_result::unsupported_feature ||
      line_primitive.diagnostic.empty()) {
    return 5;
  }

  const auto empty = asset_import::inspect_gltf({});
  if (empty.result != asset_import::inspect_result::invalid_argument) {
    return 6;
  }

  return 0;
}
