// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "../support/async_scene_reload.hpp"
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

  gneiss::runtime::application_configuration config;
  config.asset_root = root_text;
  gneiss::engine::task_scheduler scheduler({.mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance_id scene = {};
  gneiss::scene_node_id camera_before;
  gneiss::scene_node_id old_child;
  gneiss::entity_id camera_entity_before;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  if (gneiss::runtime::application::create(config, application) != gneiss::engine::error::success ||
      gneiss::test_support::load_scene(*application, uri, &scene, &scheduler) !=
          gneiss::engine::error::success ||
      application->scenes()->find_node(scene, camera_uuid, camera_before) !=
          gneiss::engine::error::success ||
      application->scenes()->find_node(scene, old_child_uuid, old_child) !=
          gneiss::engine::error::success ||
      application->native_world()->node_get_entity(camera_before, camera_entity_before) !=
          gneiss::engine::error::success) {
    return 1;
  }

  const auto retired_child = old_child;
  write_scene(scene_path, "{}");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, uri, false) !=
      gneiss::engine::error::success) {
    return 2;
  }
  gneiss::scene_node_id camera_after;
  gneiss::scene_node_id new_child;
  gneiss::entity_id camera_entity_after;
  std::uint64_t count = 0U;
  if (application->scenes()->find_node(scene, camera_uuid, camera_after) !=
          gneiss::engine::error::success ||
      camera_after != camera_before ||
      application->native_world()->node_get_entity(camera_after, camera_entity_after) !=
          gneiss::engine::error::success ||
      camera_entity_after != camera_entity_before ||
      application->scenes()->find_node(scene, new_child_uuid, new_child) !=
          gneiss::engine::error::success ||
      application->scenes()->find_node(scene, old_child_uuid, old_child) !=
          gneiss::engine::error::not_found ||
      application->native_world()->node_get_parent(retired_child, camera_after) !=
          gneiss::engine::error::invalid_handle ||
      application->scenes()->get_node_count(scene, &count) != gneiss::engine::error::success ||
      count != 2U) {
    return 3;
  }

  write_scene(
      scene_path,
      R"({"mesh_renderer":{"mesh":"asset://models/missing.mesh.json","material":"asset://materials/triangle.material.json"}})");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, uri, false) !=
          gneiss::engine::error::not_found ||
      application->scenes()->find_node(scene, camera_uuid, camera_after) !=
          gneiss::engine::error::success ||
      camera_after != camera_before ||
      application->scenes()->find_node(scene, new_child_uuid, new_child) !=
          gneiss::engine::error::success) {
    return 4;
  }

  const bool destroyed = application->scenes()->unload(scene) == gneiss::engine::error::success &&
                         application->shutdown() == gneiss::engine::error::success;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 5;
} catch (...) {
  return 99;
}
