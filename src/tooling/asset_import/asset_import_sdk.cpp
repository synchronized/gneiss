// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_import_sdk.h"

#include "tooling/asset_import/asset_writer.h"
#include "tooling/asset_import/gltf_importer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace gneiss::tooling::asset_import {
namespace {

[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& candidate) {
  const auto relative = candidate.lexically_relative(root);
  return !relative.empty() && relative != "." && *relative.begin() != "..";
}

[[nodiscard]] std::string stable_source_key(const std::filesystem::path& relative_source) {
  constexpr std::uint64_t offset_basis = 14695981039346656037ULL;
  constexpr std::uint64_t prime = 1099511628211ULL;
  auto hash = offset_basis;
  const auto normalized = relative_source.generic_u8string();
  for (const auto character : normalized) {
    hash ^= static_cast<std::uint8_t>(character);
    hash *= prime;
  }
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

[[nodiscard]] import_asset_result map_result(inspect_result result) {
  switch (result) {
  case inspect_result::success:
    return import_asset_result::success;
  case inspect_result::invalid_argument:
    return import_asset_result::invalid_argument;
  case inspect_result::source_unavailable:
    return import_asset_result::source_unavailable;
  case inspect_result::invalid_source:
    return import_asset_result::invalid_source;
  case inspect_result::unsupported_feature:
    return import_asset_result::unsupported_feature;
  }
  return import_asset_result::invalid_source;
}

[[nodiscard]] import_asset_report failure(import_asset_result result, std::string diagnostic) {
  import_asset_report report;
  report.result = result;
  report.diagnostic = std::move(diagnostic);
  return report;
}

[[nodiscard]] std::vector<std::string> collect_output_uris(const import_ir& data,
                                                           std::string_view source_key) {
  const auto prefix = std::string{"asset://imported/"} + std::string{source_key} + '/';
  std::vector<std::string> outputs;
  for (std::size_t mesh_index = 0; mesh_index < data.meshes.size(); ++mesh_index) {
    for (std::size_t primitive_index = 0;
         primitive_index < data.meshes[mesh_index].primitives.size(); ++primitive_index) {
      outputs.push_back(prefix + "models/mesh-" + std::to_string(mesh_index) + "-primitive-" +
                        std::to_string(primitive_index) + ".gneiss-mesh");
    }
  }
  for (std::size_t index = 0; index < data.materials.size(); ++index) {
    outputs.push_back(prefix + "materials/material-" + std::to_string(index) + ".material.json");
  }
  const bool needs_default = std::ranges::any_of(data.meshes, [](const import_ir_mesh& mesh) {
    return std::ranges::any_of(mesh.primitives, [](const import_ir_primitive& primitive) {
      return !primitive.material_index;
    });
  });
  if (needs_default) {
    outputs.push_back(prefix + "materials/default.material.json");
  }
  for (std::size_t index = 0; index < data.images.size(); ++index) {
    const auto name = "textures/image-" + std::to_string(index);
    outputs.push_back(prefix + name + ".png");
    outputs.push_back(prefix + name + ".texture.json");
  }
  outputs.push_back(prefix + "scenes/scene.scene.json");
  return outputs;
}

} // namespace

import_asset_report import_project_asset(const import_asset_request& request) {
  if (request.source_root.empty() || request.imported_root.empty() || request.source_path.empty()) {
    return failure(import_asset_result::invalid_argument, "源目录、派生目录和源文件均不能为空");
  }

  try {
    const auto source_root = std::filesystem::weakly_canonical(request.source_root);
    const auto source_path = std::filesystem::weakly_canonical(request.source_path);
    if (!is_within(source_root, source_path)) {
      return failure(import_asset_result::invalid_argument, "源文件必须位于工程 sources 目录内");
    }
    if (!std::filesystem::is_regular_file(source_path)) {
      return failure(import_asset_result::source_unavailable, "源文件不存在或不是普通文件");
    }
    const auto extension = source_path.extension().string();
    if (extension != ".gltf" && extension != ".glb") {
      return failure(import_asset_result::unsupported_feature, "当前导入 SDK 仅支持 glTF 和 GLB");
    }

    const auto relative_source = source_path.lexically_relative(source_root);
    const auto source_key = stable_source_key(relative_source);
    const auto output_directory =
        std::filesystem::absolute(request.imported_root).lexically_normal() / source_key;
    auto inspected = inspect_gltf(source_path);
    if (inspected.result != inspect_result::success) {
      auto report = failure(map_result(inspected.result), std::move(inspected.diagnostic));
      report.summary = inspected.summary;
      report.source_key = source_key;
      report.output_directory = output_directory;
      return report;
    }
    const auto asset_uri_prefix = std::string{"asset://imported/"} + source_key + '/';
    auto written = write_assets(inspected.data, output_directory, asset_uri_prefix);
    if (!written.success) {
      auto report = failure(import_asset_result::write_failed, std::move(written.diagnostic));
      report.summary = inspected.summary;
      report.source_key = source_key;
      report.output_directory = output_directory;
      return report;
    }
    return {.result = import_asset_result::success,
            .summary = inspected.summary,
            .source_key = source_key,
            .output_directory = output_directory,
            .output_uris = collect_output_uris(inspected.data, source_key),
            .diagnostic = {}};
  } catch (const std::exception& error) {
    return failure(import_asset_result::source_unavailable,
                   std::string{"解析导入路径失败："} + error.what());
  }
}

} // namespace gneiss::tooling::asset_import
