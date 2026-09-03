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

  std::uint64_t prefab_node_count = 0;
  gneiss_scene_prefab_node_info prefab_root = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  gneiss_scene_prefab_node_info prefab_source = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  if (gneiss_scene_instance_get_prefab_node_count(application, scene, &prefab_node_count) !=
          GNEISS_SUCCESS ||
      prefab_node_count != 2U ||
      gneiss_scene_instance_get_prefab_node_info(application, scene, 0U, &prefab_root) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_get_prefab_node_info(application, scene, 1U, &prefab_source) !=
          GNEISS_SUCCESS ||
      prefab_root.flags != GNEISS_SCENE_PREFAB_NODE_INSTANCE_ROOT ||
      (prefab_source.flags & GNEISS_SCENE_PREFAB_NODE_SOURCE_READ_ONLY) == 0U ||
      (prefab_source.flags & GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN) == 0U ||
      prefab_source.parent != prefab_root.node ||
      std::string_view(prefab_root.instance_uuid, prefab_root.instance_uuid_length) !=
          "30000000-0000-4000-8000-000000000012" ||
      std::string_view(prefab_source.source_node_uuid, prefab_source.source_node_uuid_length) !=
          "30000000-0000-4000-8000-000000000002" ||
      prefab_source.local_transform.translation[0] != 3.0F) {
    return 2;
  }

  gneiss_transform source_transform = GNEISS_TRANSFORM_IDENTITY;
  source_transform.translation[0] = 1.0F;
  if (gneiss_scene_instance_set_prefab_source_transform(application, scene, prefab_source.node,
                                                        &source_transform) != GNEISS_SUCCESS) {
    return 2;
  }
  std::uint64_t sparse_length = 0U;
  if (gneiss_scene_instance_serialize(application, scene, nullptr, 0U, &sparse_length) !=
          GNEISS_SUCCESS ||
      sparse_length == 0U) {
    return 2;
  }
  std::string sparse_json(sparse_length, '\0');
  if (gneiss_scene_instance_serialize(application, scene, sparse_json.data(), sparse_json.size(),
                                      &sparse_length) != GNEISS_SUCCESS ||
      sparse_json.find(R"("overrides":[])") == std::string::npos) {
    return 2;
  }
  source_transform.translation[0] = 4.0F;
  if (gneiss_scene_instance_set_prefab_source_transform(application, scene, prefab_source.node,
                                                        &source_transform) != GNEISS_SUCCESS) {
    return 2;
  }

  const auto stale_loaded_root = prefab_root.node;
  gneiss_scene_prefab_refresh_token loaded_refresh = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  if (gneiss_scene_instance_refresh_prefab_instance(application, scene, stale_loaded_root,
                                                    &prefab_root.node,
                                                    &loaded_refresh) != GNEISS_SUCCESS ||
      prefab_root.node == GNEISS_NULL_SCENE_NODE_ID || prefab_root.node == stale_loaded_root ||
      gneiss_scene_instance_get_prefab_node_info(application, scene, 1U, &prefab_source) !=
          GNEISS_SUCCESS ||
      prefab_source.local_transform.translation[0] != 4.0F ||
      gneiss_scene_instance_release_prefab_refresh(application, scene, loaded_refresh) !=
          GNEISS_SUCCESS) {
    return 2;
  }

  gneiss_scene_node_id anchor = GNEISS_NULL_SCENE_NODE_ID;
  constexpr std::string_view anchor_uuid = "30000000-0000-4000-8000-000000000011";
  constexpr std::string_view created_uuid = "30000000-0000-4000-8000-000000000013";
  constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
  constexpr std::string_view created_name = "Second Lamp";
  gneiss_scene_prefab_instance_desc create_desc = GNEISS_SCENE_PREFAB_INSTANCE_DESC_INIT;
  create_desc.instance_uuid = created_uuid.data();
  create_desc.instance_uuid_length = created_uuid.size();
  create_desc.name = created_name.data();
  create_desc.name_length = created_name.size();
  create_desc.prefab_uri = prefab_uri.data();
  create_desc.prefab_uri_length = prefab_uri.size();
  gneiss_scene_node_id created_root = GNEISS_NULL_SCENE_NODE_ID;
  if (gneiss_scene_instance_find_node(application, scene, anchor_uuid.data(), anchor_uuid.size(),
                                      &anchor) != GNEISS_SUCCESS) {
    return 3;
  }
  create_desc.parent = prefab_root.node;
  if (gneiss_scene_instance_create_prefab_instance(application, scene, &create_desc,
                                                   &created_root) != GNEISS_ERROR_INVALID_HANDLE ||
      created_root != GNEISS_NULL_SCENE_NODE_ID) {
    return 3;
  }
  constexpr std::string_view missing_prefab_uri = "asset://prefabs/not-found.prefab.json";
  create_desc.parent = anchor;
  create_desc.prefab_uri = missing_prefab_uri.data();
  create_desc.prefab_uri_length = missing_prefab_uri.size();
  if (gneiss_scene_instance_create_prefab_instance(application, scene, &create_desc,
                                                   &created_root) != GNEISS_ERROR_NOT_FOUND ||
      created_root != GNEISS_NULL_SCENE_NODE_ID ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 3U) {
    return 3;
  }
  create_desc.prefab_uri = prefab_uri.data();
  create_desc.prefab_uri_length = prefab_uri.size();
  create_desc.parent = anchor;
  if (gneiss_scene_instance_create_prefab_instance(application, scene, &create_desc,
                                                   &created_root) != GNEISS_SUCCESS ||
      created_root == GNEISS_NULL_SCENE_NODE_ID ||
      gneiss_scene_instance_get_prefab_node_count(application, scene, &prefab_node_count) !=
          GNEISS_SUCCESS ||
      prefab_node_count != 4U ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 5U) {
    return 3;
  }
  constexpr std::string_view renamed = "Renamed Lamp";
  gneiss_transform moved = GNEISS_TRANSFORM_IDENTITY;
  moved.translation[0] = 4.0F;
  if (gneiss_scene_instance_set_prefab_instance_name(
          application, scene, created_root, renamed.data(), renamed.size()) != GNEISS_SUCCESS ||
      gneiss_scene_node_set_local_transform(world, created_root, &moved) != GNEISS_SUCCESS) {
    return 4;
  }
  const auto stale_root = created_root;
  gneiss_entity_id stale_entity = GNEISS_NULL_ENTITY_ID;
  gneiss_scene_prefab_refresh_token refresh_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  if (gneiss_scene_instance_refresh_prefab_instance(application, scene, stale_root, &created_root,
                                                    &refresh_token) != GNEISS_SUCCESS ||
      created_root == GNEISS_NULL_SCENE_NODE_ID || created_root == stale_root ||
      refresh_token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN ||
      gneiss_scene_node_get_entity(world, stale_root, &stale_entity) !=
          GNEISS_ERROR_INVALID_HANDLE) {
    return 4;
  }
  const auto refreshed_root = created_root;
  if (gneiss_scene_instance_toggle_prefab_refresh(application, scene, refresh_token,
                                                  &created_root) != GNEISS_SUCCESS ||
      created_root == refreshed_root ||
      gneiss_scene_instance_toggle_prefab_refresh(application, scene, refresh_token,
                                                  &created_root) != GNEISS_SUCCESS ||
      created_root == GNEISS_NULL_SCENE_NODE_ID ||
      gneiss_scene_instance_release_prefab_refresh(application, scene, refresh_token) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_destroy_prefab_instance(application, scene, prefab_root.node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_get_prefab_node_count(application, scene, &prefab_node_count) !=
          GNEISS_SUCCESS ||
      prefab_node_count != 2U) {
    return 4;
  }

  std::uint64_t json_length = 0;
  if (gneiss_scene_instance_serialize(application, scene, nullptr, 0U, &json_length) !=
          GNEISS_SUCCESS ||
      json_length == 0U) {
    return 5;
  }
  std::string json(json_length, '\0');
  if (gneiss_scene_instance_serialize(application, scene, json.data(), json.size(), &json_length) !=
          GNEISS_SUCCESS ||
      json.find("asset://prefabs/test.prefab.json") == std::string::npos ||
      json.find(created_uuid) == std::string::npos || json.find(renamed) == std::string::npos ||
      json.find("30000000-0000-4000-8000-000000000002") != std::string::npos) {
    return 6;
  }

  if (gneiss_scene_instance_unload(application, scene) != GNEISS_SUCCESS ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U) {
    return 7;
  }
  scene = GNEISS_NULL_SCENE_INSTANCE;
  if (gneiss_scene_instance_load(application, missing_scene_uri.data(), missing_scene_uri.size(),
                                 &scene) != GNEISS_ERROR_NOT_FOUND ||
      scene != GNEISS_NULL_SCENE_INSTANCE ||
      gneiss_world_entity_count(world, &entity_count) != GNEISS_SUCCESS || entity_count != 0U) {
    return 8;
  }
  return gneiss_application_destroy(application) == GNEISS_SUCCESS ? 0 : 9;
} catch (...) {
  return 99;
}
