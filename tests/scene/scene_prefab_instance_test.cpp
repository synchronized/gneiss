// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.h>
#include <gneiss/scene.h>
#include <gneiss/world.h>

#include <cstdint>
#include <string>
#include <string_view>

int main() try {
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  constexpr std::string_view missing_scene_uri = "asset://scenes/missing-prefab.scene.json";
  gneiss_application_desc application_desc = GNEISS_APPLICATION_DESC_INIT;
  application_desc.asset_root = asset_root.data();
  application_desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss_application application = GNEISS_NULL_APPLICATION;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  std::uint64_t entity_count = 0;
  if (gneiss_application_create(&application_desc, &application) != GNEISS_SUCCESS ||
      gneiss_application_get_world(application, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 3U) {
    return 1;
  }

  std::uint64_t json_length = 0;
  if (gneiss_scene_instance_serialize(application, scene, nullptr, 0U, &json_length) !=
          GNEISS_SUCCESS ||
      json_length == 0U) {
    return 2;
  }
  std::string json(json_length, '\0');
  if (gneiss_scene_instance_serialize(application, scene, json.data(), json.size(), &json_length) !=
          GNEISS_SUCCESS ||
      json.find("asset://prefabs/test.prefab.json") == std::string::npos ||
      json.find("30000000-0000-4000-8000-000000000002") != std::string::npos) {
    return 3;
  }

  if (gneiss_scene_instance_unload(application, scene) != GNEISS_SUCCESS ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U) {
    return 4;
  }
  scene = GNEISS_NULL_SCENE_INSTANCE;
  if (gneiss_scene_instance_load(application, missing_scene_uri.data(), missing_scene_uri.size(),
                                 &scene) != GNEISS_ERROR_NOT_FOUND ||
      scene != GNEISS_NULL_SCENE_INSTANCE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U) {
    return 5;
  }
  return gneiss_application_destroy(application) == GNEISS_SUCCESS ? 0 : 6;
} catch (...) {
  return 99;
}
