// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include <array>
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
      valid.data.meshes[0].primitives[0].index_accessor != 3U ||
      valid.data.meshes[0].primitives[0].vertices.size() != 3U ||
      valid.data.meshes[0].primitives[0].indices != std::vector<std::uint32_t>{0U, 1U, 2U} ||
      valid.data.nodes[0].translation != std::array<float, 3>{1.0F, 2.0F, 3.0F} ||
      valid.data.nodes[0].scale != std::array<float, 3>{2.0F, 2.0F, 2.0F} ||
      valid.data.materials.size() != 1U ||
      valid.data.materials[0].base_color != std::array<float, 4>{0.5F, 0.6F, 0.7F, 1.0F} ||
      valid.data.materials[0].base_color_image_index != 0U || valid.data.images.size() != 1U ||
      !valid.data.images[0].is_png) {
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

  const auto invalid_index = asset_import::inspect_gltf(root / "invalid_index.gltf");
  if (invalid_index.result != asset_import::inspect_result::invalid_source ||
      invalid_index.diagnostic.empty()) {
    return 6;
  }

  const auto non_finite = asset_import::inspect_gltf(root / "non_finite_vertex.gltf");
  if (non_finite.result != asset_import::inspect_result::invalid_source ||
      non_finite.diagnostic.empty()) {
    return 7;
  }

  const auto invalid_transform = asset_import::inspect_gltf(root / "invalid_transform.gltf");
  if (invalid_transform.result != asset_import::inspect_result::unsupported_feature ||
      invalid_transform.diagnostic.empty()) {
    return 8;
  }

  const auto non_png = asset_import::inspect_gltf(root / "non_png_texture.gltf");
  if (non_png.result != asset_import::inspect_result::unsupported_feature ||
      non_png.diagnostic.empty()) {
    return 9;
  }

  const auto empty = asset_import::inspect_gltf({});
  if (empty.result != asset_import::inspect_result::invalid_argument) {
    return 10;
  }

  return 0;
}
