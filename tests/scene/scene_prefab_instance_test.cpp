// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include <cstdint>
#include <string>
#include <string_view>

int main() try {
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  constexpr std::string_view missing_scene_uri = "asset://scenes/missing-prefab.scene.json";
  gneiss::runtime::application_configuration application_desc;
  application_desc.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  std::uint64_t entity_count = 0;
  if (gneiss::runtime::application::create(application_desc, application) !=
          gneiss::engine::result::success ||
      application->scenes()->load(scene_uri, &scene) != gneiss::engine::result::success ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 3U) {
    return 1;
  }

  std::uint64_t prefab_node_count = 0;
  gneiss::engine::prefab_node_view prefab_root;
  gneiss::engine::prefab_node_view prefab_source;
  if (application->scenes()->get_prefab_node_count(scene, &prefab_node_count) !=
          gneiss::engine::result::success ||
      prefab_node_count != 2U ||
      application->scenes()->get_prefab_node_info(scene, 0U, prefab_root) !=
          gneiss::engine::result::success ||
      application->scenes()->get_prefab_node_info(scene, 1U, prefab_source) !=
          gneiss::engine::result::success ||
      prefab_root.flags != gneiss::scene_prefab_flags::instance_root ||
      (prefab_source.flags & gneiss::scene_prefab_flags::source_read_only) ==
          gneiss::scene_prefab_flags::none ||
      (prefab_source.flags & gneiss::scene_prefab_flags::translation_overridden) ==
          gneiss::scene_prefab_flags::none ||
      prefab_source.parent != prefab_root.node ||
      prefab_root.instance_uuid != "30000000-0000-4000-8000-000000000012" ||
      prefab_source.source_node_uuid != "30000000-0000-4000-8000-000000000002" ||
      prefab_source.local_transform.translation[0] != 3.0F) {
    return 2;
  }

  gneiss::engine::transform source_transform;
  source_transform.translation[0] = 1.0F;
  if (application->scenes()->set_prefab_source_transform(
          scene, prefab_source.node, source_transform) != gneiss::engine::result::success) {
    return 2;
  }
  std::string sparse_json;
  if (application->scenes()->serialize(scene, sparse_json) != gneiss::engine::result::success ||
      sparse_json.empty() || sparse_json.find(R"("overrides":[])") == std::string::npos) {
    return 2;
  }
  source_transform.translation[0] = 4.0F;
  if (application->scenes()->set_prefab_source_transform(
          scene, prefab_source.node, source_transform) != gneiss::engine::result::success) {
    return 2;
  }

  const auto stale_loaded_root = prefab_root.node;
  gneiss_scene_prefab_refresh_token loaded_refresh = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  if (application->scenes()->refresh_prefab_instance(scene, stale_loaded_root, prefab_root.node,
                                                     &loaded_refresh) !=
          gneiss::engine::result::success ||
      prefab_root.node == gneiss::null_scene_node_id || prefab_root.node == stale_loaded_root ||
      application->scenes()->get_prefab_node_info(scene, 1U, prefab_source) !=
          gneiss::engine::result::success ||
      prefab_source.local_transform.translation[0] != 4.0F ||
      application->scenes()->release_prefab_refresh(scene, loaded_refresh) !=
          gneiss::engine::result::success) {
    return 2;
  }

  gneiss::scene_node_id anchor;
  constexpr std::string_view anchor_uuid = "30000000-0000-4000-8000-000000000011";
  constexpr std::string_view created_uuid = "30000000-0000-4000-8000-000000000013";
  constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
  constexpr std::string_view created_name = "Second Lamp";
  gneiss::engine::prefab_creation create_desc;
  create_desc.instance_uuid = created_uuid;
  create_desc.name = created_name;
  create_desc.prefab_uri = prefab_uri;
  gneiss::scene_node_id created_root;
  if (application->scenes()->find_node(scene, anchor_uuid, anchor) !=
      gneiss::engine::result::success) {
    return 3;
  }
  create_desc.parent = prefab_root.node;
  if (application->scenes()->create_prefab_instance(scene, create_desc, created_root) !=
          gneiss::engine::result::invalid_handle ||
      created_root != gneiss::null_scene_node_id) {
    return 3;
  }
  constexpr std::string_view missing_prefab_uri = "asset://prefabs/not-found.prefab.json";
  create_desc.parent = anchor;
  create_desc.prefab_uri = missing_prefab_uri;
  if (application->scenes()->create_prefab_instance(scene, create_desc, created_root) !=
          gneiss::engine::result::not_found ||
      created_root != gneiss::null_scene_node_id ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 3U) {
    return 3;
  }
  create_desc.prefab_uri = prefab_uri;
  create_desc.parent = anchor;
  if (application->scenes()->create_prefab_instance(scene, create_desc, created_root) !=
          gneiss::engine::result::success ||
      created_root == gneiss::null_scene_node_id ||
      application->scenes()->get_prefab_node_count(scene, &prefab_node_count) !=
          gneiss::engine::result::success ||
      prefab_node_count != 4U ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 5U) {
    return 3;
  }
  constexpr std::string_view renamed = "Renamed Lamp";
  gneiss::engine::transform moved;
  moved.translation[0] = 4.0F;
  if (application->scenes()->set_prefab_instance_name(scene, created_root, renamed) !=
          gneiss::engine::result::success ||
      application->native_world()->node_set_local_transform(created_root, moved) !=
          gneiss::engine::result::success) {
    return 4;
  }
  const auto stale_root = created_root;
  gneiss::entity_id stale_entity;
  gneiss_scene_prefab_refresh_token refresh_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  if (application->scenes()->refresh_prefab_instance(
          scene, stale_root, created_root, &refresh_token) != gneiss::engine::result::success ||
      created_root == gneiss::null_scene_node_id || created_root == stale_root ||
      refresh_token == GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN ||
      application->native_world()->node_get_entity(stale_root, stale_entity) !=
          gneiss::engine::result::invalid_handle) {
    return 4;
  }
  const auto refreshed_root = created_root;
  if (application->scenes()->toggle_prefab_refresh(scene, refresh_token, created_root) !=
          gneiss::engine::result::success ||
      created_root == refreshed_root ||
      application->scenes()->toggle_prefab_refresh(scene, refresh_token, created_root) !=
          gneiss::engine::result::success ||
      created_root == gneiss::null_scene_node_id ||
      application->scenes()->release_prefab_refresh(scene, refresh_token) !=
          gneiss::engine::result::success ||
      application->scenes()->destroy_prefab_instance(scene, prefab_root.node) !=
          gneiss::engine::result::success ||
      application->scenes()->get_prefab_node_count(scene, &prefab_node_count) !=
          gneiss::engine::result::success ||
      prefab_node_count != 2U) {
    return 4;
  }

  std::string json;
  if (application->scenes()->serialize(scene, json) != gneiss::engine::result::success ||
      json.empty() || json.find("asset://prefabs/test.prefab.json") == std::string::npos ||
      json.find(created_uuid) == std::string::npos || json.find(renamed) == std::string::npos ||
      json.find("30000000-0000-4000-8000-000000000002") != std::string::npos) {
    return 6;
  }
  if (application->scenes()->unload(scene) != gneiss::engine::result::success ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 0U) {
    return 7;
  }
  scene = GNEISS_NULL_SCENE_INSTANCE;
  if (application->scenes()->load(missing_scene_uri, &scene) != gneiss::engine::result::not_found ||
      scene != GNEISS_NULL_SCENE_INSTANCE ||
      application->native_world()->entity_count(entity_count) != gneiss::engine::result::success ||
      entity_count != 0U) {
    return 8;
  }
  return application->shutdown(0) == gneiss::engine::result::success ? 0 : 9;
} catch (...) {
  return 99;
}
