// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_prefab.hpp"
#include "../support/async_scene_reload.hpp"
#include "../support/prefab_query.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

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

gneiss::engine::error find_source(const std::shared_ptr<gneiss::runtime::application>& application,
                                  gneiss::scene_instance_id scene, std::string_view instance_uuid,
                                  std::string_view source_uuid,
                                  gneiss::engine::scene_node_view& output) {
  std::uint64_t count = 0U;
  auto result = gneiss::engine::error{
      gneiss::test_support::prefab_node_count(*application->scenes(), scene, &count)};
  for (std::uint64_t index = 0U; result == gneiss::engine::error::success && index < count;
       ++index) {
    output = {};
    result = gneiss::engine::error{
        gneiss::test_support::prefab_node_info(*application->scenes(), scene, index, output)};
    if (result == gneiss::engine::error::success && !output.prefab->source_node_uuid.empty() &&
        output.prefab->instance_uuid == instance_uuid &&
        output.prefab->source_node_uuid == source_uuid) {
      return gneiss::engine::error::success;
    }
  }
  return result == gneiss::engine::error::success ? gneiss::engine::error::not_found : result;
}

} // namespace

int main() try {
  const auto root = std::filesystem::temp_directory_path() / "gneiss-prefab-structural-reload-test";
  std::filesystem::remove_all(root);
  std::filesystem::copy(GNEISS_TEST_ASSET_ROOT, root, std::filesystem::copy_options::recursive);
  const auto root_text = root.generic_string();
  gneiss::runtime::application_configuration config;
  config.asset_root = root_text;
  gneiss::engine::task_scheduler scheduler({.mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance_id scene = {};
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  if (gneiss::runtime::application::create(config, application) != gneiss::engine::error::success ||
      gneiss::engine::error{gneiss::test_support::load_scene(
          *application, scene_uri, &scene, &scheduler)} != gneiss::engine::error::success) {
    return 1;
  }

  gneiss::engine::prefab_creation create;
  create.instance_uuid = second_instance_uuid;
  create.prefab_uri = prefab_uri;
  gneiss::scene_node_id second_root;
  if (gneiss::test_support::create_prefab(*application, scene, create, second_root, &scheduler)
          .failed()) {
    return 2;
  }

  gneiss::engine::scene_node_view first_before;
  gneiss::engine::scene_node_view second_before;
  if (find_source(application, scene, first_instance_uuid, source_root_uuid, first_before) !=
          gneiss::engine::error::success ||
      find_source(application, scene, second_instance_uuid, source_root_uuid, second_before) !=
          gneiss::engine::error::success) {
    return 3;
  }

  const auto prefab_path = root / "prefabs" / "test.prefab.json";
  write_prefab(prefab_path, source_root_uuid, true);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, prefab_uri,
                                                  true) != gneiss::engine::error::success) {
    return 4;
  }
  gneiss::engine::scene_node_view first_after;
  gneiss::engine::scene_node_view second_after;
  gneiss::engine::scene_node_view child;
  std::uint64_t count = 0U;
  if (find_source(application, scene, first_instance_uuid, source_root_uuid, first_after) !=
          gneiss::engine::error::success ||
      find_source(application, scene, second_instance_uuid, source_root_uuid, second_after) !=
          gneiss::engine::error::success ||
      first_after.node != first_before.node || first_after.entity != first_before.entity ||
      second_after.node != second_before.node || second_after.entity != second_before.entity ||
      first_after.name != "Reloaded Root" || second_after.name != "Reloaded Root" ||
      first_after.local_transform.translation[0] != 3.0F ||
      second_after.local_transform.translation[0] != 2.0F ||
      find_source(application, scene, first_instance_uuid, source_child_uuid, child) !=
          gneiss::engine::error::success ||
      find_source(application, scene, second_instance_uuid, source_child_uuid, child) !=
          gneiss::engine::error::success ||
      gneiss::test_support::prefab_node_count(*application->scenes(), scene, &count) !=
          gneiss::engine::error::success ||
      count != 6U) {
    return 5;
  }

  constexpr std::string_view replacement_root_uuid = "30000000-0000-4000-8000-000000000099";
  write_prefab(prefab_path, replacement_root_uuid, false);
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, prefab_uri,
                                                  true) != gneiss::engine::error::not_found ||
      find_source(application, scene, first_instance_uuid, source_root_uuid, first_after) !=
          gneiss::engine::error::success ||
      first_after.node != first_before.node ||
      find_source(application, scene, second_instance_uuid, source_root_uuid, second_after) !=
          gneiss::engine::error::success ||
      second_after.node != second_before.node) {
    return 6;
  }

  const bool destroyed = application->scenes()->unload(scene) == gneiss::engine::error::success &&
                         application->shutdown() == gneiss::engine::error::success;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 7;
} catch (...) {
  return 99;
}
