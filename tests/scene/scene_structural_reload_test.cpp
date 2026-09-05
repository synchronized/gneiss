// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "application/application_asset_reload_internal.h"

#include <gneiss/application.h>
#include <gneiss/scene.h>
#include <gneiss/world.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view scene_uuid = "00000000-0000-4000-8000-000000000001";
constexpr std::string_view camera_uuid = "00000000-0000-4000-8000-000000000002";
constexpr std::string_view old_child_uuid = "00000000-0000-4000-8000-000000000003";
constexpr std::string_view new_child_uuid = "00000000-0000-4000-8000-000000000004";

void write_scene(const std::filesystem::path& path, std::string_view child_components) {
  const std::string json =
      std::string{R"({"format":"gneiss.scene","version":4,"scene_uuid":")"} +
      std::string{scene_uuid} + R"(","objects":[{"uuid":")" + std::string{camera_uuid} +
      R"(","name":"Main Camera","parent":null,"transform":{"translation":[4.0,0.0,2.0],"rotation":[0.0,0.0,0.0,1.0],"scale":[1.0,1.0,1.0]},"components":{"camera":{"vertical_field_of_view_radians":1.0,"near_plane":0.2,"far_plane":500.0,"is_primary":true}}},{"uuid":")" +
      std::string{new_child_uuid} + R"(","name":"New Child","parent":")" +
      std::string{camera_uuid} +
      R"(","transform":{"translation":[1.0,2.0,3.0],"rotation":[0.0,0.0,0.0,1.0],"scale":[1.0,1.0,1.0]},"components":)" +
      std::string{child_components} + R"(}],"prefab_instances":[]})";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(json.data(), static_cast<std::streamsize>(json.size()));
}

} // namespace

int main() try {
  const auto root = std::filesystem::temp_directory_path() / "gneiss-scene-structural-reload-test";
  std::filesystem::remove_all(root);
  std::filesystem::copy(GNEISS_TEST_ASSET_ROOT, root, std::filesystem::copy_options::recursive);
  const auto scene_path = root / "scenes" / "triangle.scene.json";
  const auto root_text = root.generic_string();

  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root_text.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root_text.size());
  gneiss_application application = GNEISS_NULL_APPLICATION;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  gneiss_scene_node_id camera_before = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id old_child = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_entity_id camera_entity_before = GNEISS_NULL_ENTITY_ID;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_get_world(application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, uri.data(), uri.size(), &scene) != GNEISS_SUCCESS ||
      gneiss_scene_instance_find_node(application, scene, camera_uuid.data(), camera_uuid.size(),
                                      &camera_before) != GNEISS_SUCCESS ||
      gneiss_scene_instance_find_node(application, scene, old_child_uuid.data(),
                                      old_child_uuid.size(), &old_child) != GNEISS_SUCCESS ||
      gneiss_scene_node_get_entity(world, camera_before, &camera_entity_before) != GNEISS_SUCCESS) {
    return 1;
  }

  write_scene(scene_path, "{}");
  if (gneiss::application_internal::reload_scene(application, scene, uri) != GNEISS_SUCCESS) {
    return 2;
  }
  gneiss_scene_node_id camera_after = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id new_child = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_entity_id camera_entity_after = GNEISS_NULL_ENTITY_ID;
  std::uint64_t count = 0U;
  if (gneiss_scene_instance_find_node(application, scene, camera_uuid.data(), camera_uuid.size(),
                                      &camera_after) != GNEISS_SUCCESS ||
      camera_after != camera_before ||
      gneiss_scene_node_get_entity(world, camera_after, &camera_entity_after) != GNEISS_SUCCESS ||
      camera_entity_after != camera_entity_before ||
      gneiss_scene_instance_find_node(application, scene, new_child_uuid.data(),
                                      new_child_uuid.size(), &new_child) != GNEISS_SUCCESS ||
      gneiss_scene_instance_find_node(application, scene, old_child_uuid.data(),
                                      old_child_uuid.size(),
                                      &old_child) != GNEISS_ERROR_NOT_FOUND ||
      gneiss_scene_node_get_parent(world, old_child, &camera_after) !=
          GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_scene_instance_get_node_count(application, scene, &count) != GNEISS_SUCCESS ||
      count != 2U) {
    return 3;
  }

  write_scene(
      scene_path,
      R"({"mesh_renderer":{"mesh":"asset://models/missing.mesh.json","material":"asset://materials/triangle.material.json"}})");
  if (gneiss::application_internal::reload_scene(application, scene, uri) !=
          GNEISS_ERROR_NOT_FOUND ||
      gneiss_scene_instance_find_node(application, scene, camera_uuid.data(), camera_uuid.size(),
                                      &camera_after) != GNEISS_SUCCESS ||
      camera_after != camera_before ||
      gneiss_scene_instance_find_node(application, scene, new_child_uuid.data(),
                                      new_child_uuid.size(), &new_child) != GNEISS_SUCCESS) {
    return 4;
  }

  const bool destroyed = gneiss_scene_instance_unload(application, scene) == GNEISS_SUCCESS &&
                         gneiss_application_destroy(application) == GNEISS_SUCCESS;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 5;
} catch (...) {
  return 99;
}
