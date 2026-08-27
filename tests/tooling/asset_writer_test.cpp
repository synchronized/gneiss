// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "tooling/asset_import/asset_writer.h"
#include "tooling/asset_import/gltf_importer.h"

#include <gneiss/application.h>
#include <gneiss/scene.h>
#include <gneiss/world.h>

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
  namespace asset_import = gneiss::tooling::asset_import;
  const auto source = std::filesystem::path{GNEISS_TEST_GLTF_ROOT} / "static_triangle.gltf";
  const auto imported = asset_import::inspect_gltf(source);
  if (imported.result != asset_import::inspect_result::success) {
    return 1;
  }
  const auto root = std::filesystem::temp_directory_path() / "gneiss-asset-writer-test";
  const auto first = root / "first";
  const auto second = root / "second";
  const auto multi = root / "multi";
  std::filesystem::remove_all(root);
  if (!asset_import::write_assets(imported.data, first).success ||
      !asset_import::write_assets(imported.data, second).success) {
    return 2;
  }
  constexpr std::array files = {
      "models/mesh-0-primitive-0.mesh.json", "materials/material-0.material.json",
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
  if (!asset_import::write_assets(imported.data, first).success ||
      std::filesystem::exists(first / "stale.asset")) {
    return 4;
  }
  const auto preserved_scene = read_file(first / "scenes/scene.scene.json");
  auto invalid_data = imported.data;
  invalid_data.nodes[0].mesh_index = invalid_data.meshes.size();
  if (asset_import::write_assets(invalid_data, first).success ||
      read_file(first / "scenes/scene.scene.json") != preserved_scene ||
      std::filesystem::exists(root / "first.gneiss-staging") ||
      std::filesystem::exists(root / "first.gneiss-backup")) {
    return 5;
  }
  const auto asset_root = first.generic_string();
  gneiss_application_desc description = GNEISS_APPLICATION_DESC_INIT;
  description.asset_root = asset_root.data();
  description.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss_application application = GNEISS_NULL_APPLICATION;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  gneiss_world world = GNEISS_NULL_WORLD;
  std::uint64_t entity_count = 0;
  constexpr std::string_view scene_uri = "asset://scenes/scene.scene.json";
  if (gneiss_application_create(&description, &application) != GNEISS_SUCCESS ||
      gneiss_application_get_world(application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 1U ||
      gneiss_scene_instance_unload(application, scene) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 6;
  }

  auto multi_data = imported.data;
  auto second_primitive = multi_data.meshes[0].primitives[0];
  second_primitive.material_index.reset();
  multi_data.meshes[0].primitives.push_back(std::move(second_primitive));
  if (!asset_import::write_assets(multi_data, multi).success ||
      !std::filesystem::exists(multi / "models/mesh-0-primitive-1.mesh.json") ||
      !std::filesystem::exists(multi / "materials/default.material.json")) {
    return 7;
  }
  const auto multi_asset_root = multi.generic_string();
  description.asset_root = multi_asset_root.data();
  description.asset_root_length = static_cast<std::uint32_t>(multi_asset_root.size());
  application = GNEISS_NULL_APPLICATION;
  scene = GNEISS_NULL_SCENE_INSTANCE;
  world = GNEISS_NULL_WORLD;
  entity_count = 0;
  if (gneiss_application_create(&description, &application) != GNEISS_SUCCESS ||
      gneiss_application_get_world(application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 3U ||
      gneiss_scene_instance_unload(application, scene) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 8;
  }
  std::filesystem::remove_all(root);
  return 0;
}
