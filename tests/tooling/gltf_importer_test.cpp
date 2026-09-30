// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/gltf_importer.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

bool checks_material_states(const std::filesystem::path& root) {
  std::ifstream stream(root / "static_triangle.gltf", std::ios::binary);
  const std::string source{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
  const auto temporary = std::filesystem::temp_directory_path() / "gneiss-pbr-scope-test.gltf";
  const std::array<std::pair<std::string, std::string>, 7> changes{{
      {"\"name\": \"Stone\"", "\"name\": \"Stone\", \"alphaMode\": \"BLEND\""},
      {"\"name\": \"Stone\"", "\"name\": \"Stone\", \"alphaMode\": \"MASK\""},
      {"\"name\": \"Stone\"", "\"name\": \"Stone\", \"doubleSided\": true"},
      {"\"index\": 0", "\"index\": 0, \"texCoord\": 1"},
      {"\"TEXCOORD_0\": 2", "\"TEXCOORD_0\": 2, \"COLOR_0\": 1"},
      {"\"textures\": [{\"source\": 0}]",
       "\"samplers\": [{\"wrapS\": 33071}], \"textures\": [{\"source\": 0, \"sampler\": 0}]"},
      {"\"name\": \"Stone\"", "\"name\": \"Stone\", \"emissiveFactor\": [1.00000012,0,0]"},
  }};
  std::size_t case_index = 0;
  for (const auto& [from, to] : changes) {
    auto input = source;
    const auto offset = input.find(from);
    if (offset == std::string::npos)
      return false;
    input.replace(offset, from.size(), to);
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      output << input;
    }
    const auto report = gneiss::tooling::asset_import::inspect_gltf(temporary);
    std::filesystem::remove(temporary);
    using namespace gneiss::tooling::asset_import;
    if (case_index == 3U) {
      if (report.result == inspect_result::success || report.diagnostic.empty())
        return false;
    } else {
      if (report.result != inspect_result::success || report.data.materials.empty())
        return false;
      const auto& material = report.data.materials[0];
      if (case_index == 6U && (material.emissive[0] != 1.0F || report.diagnostic.empty()))
        return false;
      if ((case_index == 0U && material.alpha_mode != GNEISS_MATERIAL_ALPHA_BLEND) ||
          (case_index == 1U && material.alpha_mode != GNEISS_MATERIAL_ALPHA_MASK) ||
          (case_index == 2U && material.double_sided != 1U) ||
          (case_index == 4U && (!report.data.meshes[0].primitives[0].has_color ||
                                report.data.meshes[0].primitives[0].vertices[0].color !=
                                    std::array<float, 4>{0, 0, 1, 1})) ||
          (case_index == 5U && material.sampling[0].address_u != GNEISS_TEXTURE_ADDRESS_CLAMP))
        return false;
    }
    ++case_index;
  }
  auto input = source;
  constexpr std::string_view uv = "\"TEXCOORD_0\": 2";
  input.replace(input.find(uv), uv.size(), "\"TEXCOORD_0\": 2, \"TEXCOORD_1\": 2");
  constexpr std::string_view texture = "\"index\": 0";
  input.replace(input.find(texture), texture.size(), "\"index\": 0, \"texCoord\": 1");
  {
    std::ofstream output(temporary, std::ios::binary);
    output << input;
  }
  const auto uv_report = gneiss::tooling::asset_import::inspect_gltf(temporary);
  std::filesystem::remove(temporary);
  if (uv_report.result != gneiss::tooling::asset_import::inspect_result::success ||
      !uv_report.data.meshes[0].primitives[0].has_uv1 ||
      uv_report.data.materials[0].sampling[0].uv_set != 1U ||
      uv_report.data.meshes[0].primitives[0].vertices[1].uv1 != std::array<float, 2>{1, 0})
    return false;
  return true;
}

} // namespace

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
      valid.data.materials[0].base_color_image_index != 0U ||
      valid.data.materials[0].metallic != 0.25F || valid.data.materials[0].roughness != 0.75F ||
      valid.data.images.size() != 1U || !valid.data.images[0].is_png) {
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

  const auto path_escape = asset_import::inspect_gltf(root / "path_escape.gltf");
  if (path_escape.result != asset_import::inspect_result::invalid_source ||
      path_escape.diagnostic.empty()) {
    return 10;
  }

  const auto empty = asset_import::inspect_gltf({});
  if (empty.result != asset_import::inspect_result::invalid_argument) {
    return 11;
  }

  const auto repaired = asset_import::inspect_gltf(root / "degenerate_tangent.gltf");
  if (repaired.result != asset_import::inspect_result::success || repaired.diagnostic.empty() ||
      !repaired.data.meshes[0].primitives[0].repaired_tangents ||
      repaired.data.meshes[0].primitives[0].vertices[0].tangent != std::array<float, 4>{1, 0, 0, 1})
    return 13;
  return checks_material_states(root) ? 0 : 12;
}
