// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "../support/value_result.hpp"
#include "engine/core/rid.hpp"
#include "engine/resource/material/material_codec.hpp"
#include "tooling/asset_import/asset_writer.hpp"
#include "tooling/asset_import/gltf_importer.hpp"

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <string_view>

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape)
  namespace asset_import = gneiss::tooling;
  const auto source = std::filesystem::path{GNEISS_TEST_GLTF_ROOT} / "static_triangle.gltf";
  const auto imported = gneiss::tooling::inspect_gltf(source);
  if (imported.result != gneiss::tooling::inspect_result::success) {
    return 1;
  }
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-writer-test";
  const auto first = root / "first";
  const auto second = root / "second";
  const auto multi = root / "multi";
  std::filesystem::remove_all(root);
  if (!gneiss::tooling::write_assets(imported.data, first).success ||
      !gneiss::tooling::write_assets(imported.data, second).success) {
    return 2;
  }
  constexpr std::array files = {
      "models/mesh-0-primitive-0.gneiss-mesh", "materials/material-0.material.json",
      "textures/image-0.texture.json", "textures/image-0.png", "scenes/scene.scene.json"};
  for (const auto* file : files) {
    if (read_file(first / file) != read_file(second / file) || read_file(first / file).empty()) {
      return 3;
    }
  }
  {
    std::ofstream stale(first / "stale.asset", std::ios::binary);
    stale << "旧导入残留";
  }
  if (!gneiss::tooling::write_assets(imported.data, first).success ||
      std::filesystem::exists(first / "stale.asset")) {
    return 4;
  }
  const auto preserved_scene = read_file(first / "scenes/scene.scene.json");
  auto invalid_data = imported.data;
  invalid_data.nodes[0].mesh_index = invalid_data.meshes.size();
  if (gneiss::tooling::write_assets(invalid_data, first).success ||
      read_file(first / "scenes/scene.scene.json") != preserved_scene ||
      std::filesystem::exists(root / "first.gneiss-staging") ||
      std::filesystem::exists(root / "first.gneiss-backup")) {
    return 5;
  }
  const auto asset_root = first.generic_string();
  gneiss::runtime::application_configuration description;
  description.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance_id scene = {};
  std::uint64_t entity_count = 0;
  constexpr std::string_view scene_uri = "asset://scenes/scene.scene.json";
  if (gneiss::runtime::application::create(description, application) !=
          gneiss::engine::error::success ||
      gneiss::test_support::load_scene(*application, scene_uri, &scene) !=
          gneiss::engine::error::success ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 1U ||
      application->scenes()->unload(scene) != gneiss::engine::error::success ||
      application->shutdown() != gneiss::engine::error::success) {
    return 6;
  }

  auto multi_data = imported.data;
  auto second_primitive = multi_data.meshes[0].primitives[0];
  second_primitive.material_index.reset();
  multi_data.meshes[0].primitives.push_back(std::move(second_primitive));
  if (!gneiss::tooling::write_assets(multi_data, multi).success ||
      !std::filesystem::exists(multi / "models/mesh-0-primitive-1.gneiss-mesh") ||
      !std::filesystem::exists(multi / "materials/default.material.json")) {
    return 7;
  }
  const auto multi_asset_root = multi.generic_string();
  description.asset_root = multi_asset_root;
  application.reset();
  scene = gneiss::scene_instance_id{};
  entity_count = 0;
  if (gneiss::runtime::application::create(description, application) !=
          gneiss::engine::error::success ||
      gneiss::test_support::load_scene(*application, scene_uri, &scene) !=
          gneiss::engine::error::success ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 3U ||
      application->scenes()->unload(scene) != gneiss::engine::error::success ||
      application->shutdown() != gneiss::engine::error::success) {
    return 8;
  }
  const auto pbr = gneiss::tooling::inspect_gltf(std::filesystem::path{GNEISS_TEST_GLTF_ROOT} /
                                                 "pbr_triangle.gltf");
  const auto pbr_output = root / "pbr";
  if (pbr.result != gneiss::tooling::inspect_result::success ||
      pbr.data.materials[0].texture_indices() !=
          std::array<std::optional<std::size_t>, 5>{0U, 0U, 0U, 0U, 0U} ||
      pbr.data.materials[0].normal_scale != -0.5F ||
      pbr.data.materials[0].occlusion_strength != 0.25F ||
      pbr.data.materials[0].emissive != std::array<float, 3>{0.1F, 0.2F, 0.3F} ||
      !gneiss::tooling::write_assets(pbr.data, pbr_output).success) {
    return 9;
  }
  const auto color_image = read_file(pbr_output / "textures/image-0.png");
  if (color_image.empty() || read_file(pbr_output / "textures/image-0-linear.png") != color_image ||
      read_file(pbr_output / "textures/image-0-normal.png") != color_image ||
      read_file(pbr_output / "textures/image-0.texture.json").find("srgb") == std::string::npos ||
      read_file(pbr_output / "textures/image-0-linear.texture.json").find("linear") ==
          std::string::npos ||
      read_file(pbr_output / "textures/image-0-normal.texture.json")
              .find("\"usage\": \"normal\"") == std::string::npos) {
    return 10;
  }
  const auto material_file = read_file(pbr_output / "materials/material-0.material.json");
  auto decoded_material = gneiss::engine::decode_material(
      std::as_bytes(std::span{material_file.data(), material_file.size()}));
  if (!decoded_material ||
      decoded_material->texture_uris[1].find("image-0-linear.texture.json") == std::string::npos ||
      decoded_material->texture_uris[2].find("image-0-normal.texture.json") == std::string::npos ||
      decoded_material->texture_uris[3].empty() || decoded_material->texture_uris[4].empty() ||
      decoded_material->parameters.normal_scale != -0.5F ||
      decoded_material->parameters.occlusion_strength != 0.25F)
    return 11;
  std::filesystem::remove_all(root);
  return 0;
}
