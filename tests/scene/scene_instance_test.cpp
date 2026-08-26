// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.h>
#include <gneiss/scene.h>
#include <gneiss/world.h>

#include <cstdint>
#include <string_view>

namespace {

gneiss_application create_application(std::string_view asset_root) {
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss_application application = GNEISS_NULL_APPLICATION;
  return gneiss_application_create(&desc, &application) == GNEISS_SUCCESS ? application
                                                                          : GNEISS_NULL_APPLICATION;
}

} // namespace

int main() {
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view failure_root = GNEISS_TEST_FAILURE_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing-asset.scene.json";
  constexpr std::string_view triangle_uuid = "00000000-0000-4000-8000-000000000003";
  const auto application = create_application(asset_root);
  const auto second_application = create_application(asset_root);
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  gneiss_scene_node_id triangle = GNEISS_NULL_SCENE_NODE_ID;
  std::uint64_t entity_count = 0;
  if (application == GNEISS_NULL_APPLICATION || second_application == GNEISS_NULL_APPLICATION ||
      gneiss_application_get_world(application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS ||
      scene == GNEISS_NULL_SCENE_INSTANCE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 2U ||
      gneiss_scene_instance_find_node(application, scene, triangle_uuid.data(),
                                      triangle_uuid.size(), &triangle) != GNEISS_SUCCESS ||
      triangle == GNEISS_NULL_SCENE_NODE_ID) {
    return 1;
  }
  if (gneiss_scene_instance_unload(second_application, scene) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_scene_instance_unload(application, scene) != GNEISS_SUCCESS ||
      gneiss_scene_instance_unload(application, scene) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U) {
    return 2;
  }
  if (gneiss_application_destroy(second_application) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS) {
    return 3;
  }

  const auto failing_application = create_application(failure_root);
  world = GNEISS_NULL_WORLD;
  scene = GNEISS_NULL_SCENE_INSTANCE;
  if (failing_application == GNEISS_NULL_APPLICATION ||
      gneiss_application_get_world(failing_application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(failing_application, missing_uri.data(), missing_uri.size(),
                                 &scene) != GNEISS_ERROR_NOT_FOUND ||
      scene != GNEISS_NULL_SCENE_INSTANCE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U ||
      gneiss_application_destroy(failing_application) != GNEISS_SUCCESS) {
    return 4;
  }
  return 0;
}
