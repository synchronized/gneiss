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

constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
constexpr std::string_view source_root_uuid = "30000000-0000-4000-8000-000000000002";
constexpr std::string_view source_child_uuid = "30000000-0000-4000-8000-000000000003";
constexpr std::string_view first_instance_uuid = "30000000-0000-4000-8000-000000000012";
constexpr std::string_view second_instance_uuid = "30000000-0000-4000-8000-000000000013";

void write_prefab(const std::filesystem::path& path, std::string_view root_uuid, bool add_child) {
  std::string json =
      std::string{
          R"({"format":"gneiss.prefab","version":1,"prefab_uuid":"30000000-0000-4000-8000-000000000001","objects":[{"uuid":")"} +
      std::string{root_uuid} +
      R"(","name":"Reloaded Root","parent":null,"transform":{"translation":[2,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{}})";
  if (add_child) {
    json +=
        std::string{R"(,{"uuid":")"} + std::string{source_child_uuid} +
        R"(","name":"Reloaded Child","parent":")" + std::string{root_uuid} +
        R"(","transform":{"translation":[0,1,0],"rotation":[0,0,0,1],"scale":[1,1,1]},"components":{}})";
  }
  json += "]}";
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(json.data(), static_cast<std::streamsize>(json.size()));
}

gneiss_result find_source(gneiss_application application, gneiss_scene_instance scene,
                          std::string_view instance_uuid, std::string_view source_uuid,
                          gneiss_scene_prefab_node_info& output) {
  std::uint64_t count = 0U;
  auto result = gneiss_scene_instance_get_prefab_node_count(application, scene, &count);
  for (std::uint64_t index = 0U; result == GNEISS_SUCCESS && index < count; ++index) {
    output = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
    result = gneiss_scene_instance_get_prefab_node_info(application, scene, index, &output);
    if (result == GNEISS_SUCCESS && output.source_node_uuid != nullptr &&
        std::string_view(output.instance_uuid, output.instance_uuid_length) == instance_uuid &&
        std::string_view(output.source_node_uuid, output.source_node_uuid_length) == source_uuid) {
      return GNEISS_SUCCESS;
    }
  }
  return result == GNEISS_SUCCESS ? GNEISS_ERROR_NOT_FOUND : result;
}

} // namespace

int main() try {
  const auto root = std::filesystem::temp_directory_path() / "gneiss-prefab-structural-reload-test";
  std::filesystem::remove_all(root);
  std::filesystem::copy(GNEISS_TEST_ASSET_ROOT, root, std::filesystem::copy_options::recursive);
  const auto root_text = root.generic_string();
  gneiss_application_desc app_desc = GNEISS_APPLICATION_DESC_INIT;
  app_desc.asset_root = root_text.data();
  app_desc.asset_root_length = static_cast<std::uint32_t>(root_text.size());
  gneiss_application application = GNEISS_NULL_APPLICATION;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  if (gneiss_application_create(&app_desc, &application) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS) {
    return 1;
  }

  gneiss_scene_prefab_instance_desc create = GNEISS_SCENE_PREFAB_INSTANCE_DESC_INIT;
  create.instance_uuid = second_instance_uuid.data();
  create.instance_uuid_length = second_instance_uuid.size();
  create.prefab_uri = prefab_uri.data();
  create.prefab_uri_length = prefab_uri.size();
  gneiss_scene_node_id second_root = GNEISS_NULL_SCENE_NODE_ID;
  if (gneiss_scene_instance_create_prefab_instance(application, scene, &create, &second_root) !=
      GNEISS_SUCCESS) {
    return 2;
  }

  gneiss_scene_prefab_node_info first_before = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  gneiss_scene_prefab_node_info second_before = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  if (find_source(application, scene, first_instance_uuid, source_root_uuid, first_before) !=
          GNEISS_SUCCESS ||
      find_source(application, scene, second_instance_uuid, source_root_uuid, second_before) !=
          GNEISS_SUCCESS) {
    return 3;
  }

  const auto prefab_path = root / "prefabs" / "test.prefab.json";
  write_prefab(prefab_path, source_root_uuid, true);
  if (gneiss::application_internal::reload_prefab(application, scene, prefab_uri) !=
      GNEISS_SUCCESS) {
    return 4;
  }
  gneiss_scene_prefab_node_info first_after = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  gneiss_scene_prefab_node_info second_after = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  gneiss_scene_prefab_node_info child = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  std::uint64_t count = 0U;
  if (find_source(application, scene, first_instance_uuid, source_root_uuid, first_after) !=
          GNEISS_SUCCESS ||
      find_source(application, scene, second_instance_uuid, source_root_uuid, second_after) !=
          GNEISS_SUCCESS ||
      first_after.node != first_before.node || first_after.entity != first_before.entity ||
      second_after.node != second_before.node || second_after.entity != second_before.entity ||
      std::string_view(first_after.name, first_after.name_length) != "Reloaded Root" ||
      std::string_view(second_after.name, second_after.name_length) != "Reloaded Root" ||
      first_after.local_transform.translation[0] != 3.0F ||
      second_after.local_transform.translation[0] != 2.0F ||
      find_source(application, scene, first_instance_uuid, source_child_uuid, child) !=
          GNEISS_SUCCESS ||
      find_source(application, scene, second_instance_uuid, source_child_uuid, child) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_get_prefab_node_count(application, scene, &count) != GNEISS_SUCCESS ||
      count != 6U) {
    return 5;
  }

  constexpr std::string_view replacement_root_uuid = "30000000-0000-4000-8000-000000000099";
  write_prefab(prefab_path, replacement_root_uuid, false);
  if (gneiss::application_internal::reload_prefab(application, scene, prefab_uri) !=
          GNEISS_ERROR_NOT_FOUND ||
      find_source(application, scene, first_instance_uuid, source_root_uuid, first_after) !=
          GNEISS_SUCCESS ||
      first_after.node != first_before.node ||
      find_source(application, scene, second_instance_uuid, source_root_uuid, second_after) !=
          GNEISS_SUCCESS ||
      second_after.node != second_before.node) {
    return 6;
  }

  const bool destroyed = gneiss_scene_instance_unload(application, scene) == GNEISS_SUCCESS &&
                         gneiss_application_destroy(application) == GNEISS_SUCCESS;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 7;
} catch (...) {
  return 99;
}
