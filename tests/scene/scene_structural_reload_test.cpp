// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "../support/async_scene_reload.hpp"
#include "../support/scene_access.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

const gneiss::engine::guid scene_uuid =
    *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000001");
const gneiss::engine::guid camera_uuid =
    *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000002");
const gneiss::engine::guid old_child_uuid =
    *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000003");
const gneiss::engine::guid new_child_uuid =
    *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000004");

void write_scene(const std::filesystem::path& path, std::string_view child_components) {
  const std::string json =
      std::string{R"({"format":"gneiss.scene","version":5,"scene_uuid":")"} + scene_uuid.string() +
      R"(","objects":[{"uuid":")" + camera_uuid.string() +
      R"(","name":"Main Camera","parent":{"object":null,"source":null},"transform":{"translation":[4.0,0.0,2.0],"rotation":[0.0,0.0,0.0,1.0],"scale":[1.0,1.0,1.0]},"components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0101","$content":{"settings":{"fov":1.0,"near":0.2,"far":500.0},"is_primary":true,"uuid":"a66907af-c690-4d25-8eb5-b3a16317f536"}}]},{"uuid":")" +
      new_child_uuid.string() + R"(","name":"New Child","parent":{"object":")" +
      camera_uuid.string() +
      R"(","source":null},"transform":{"translation":[1.0,2.0,3.0],"rotation":[0.0,0.0,0.0,1.0],"scale":[1.0,1.0,1.0]},"components":)" +
      std::string{child_components} + R"(}]})";
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
  unsigned visual_failures{};
  config.callbacks.diagnostic = [&](const auto& event) {
    if (event.module == "scene.assets")
      ++visual_failures;
  };
  gneiss::engine::task_scheduler scheduler({.mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance_id scene = {};
  gneiss::runtime::scene_load_completion loaded;
  gneiss::scene_node_id camera_before;
  gneiss::scene_node_id old_child;
  gneiss::entity_id camera_entity_before;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(config),
                                         application) != gneiss::engine::error::success ||
      gneiss::test_support::activate_scene_for_test(*application, scheduler, uri, loaded) !=
          gneiss::engine::error::success ||
      gneiss::test_support::take_value(gneiss::test_support::scene_access{*application}->find_node(
                                           scene = loaded.scene, camera_uuid),
                                       camera_before) != gneiss::engine::error::success ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(scene, old_child_uuid),
          old_child) != gneiss::engine::error::success ||
      application->native_world()->node_get_entity(camera_before, camera_entity_before) !=
          gneiss::engine::error::success) {
    return 1;
  }

  // 直接修改 World 后重载同一文件，差异必须基于当前对象而非加载时描述。
  if (application->native_world()
          ->node_set_identity(camera_before, {camera_uuid, "Edited"})
          .failed() ||
      application->native_world()->entity_remove_camera(camera_entity_before).failed() ||
      application->native_world()
          ->find(gneiss::entity_id{(old_child).get()})
          .reparent(gneiss::engine::game_object{})
          .failed() ||
      gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, uri, false)
          .failed())
    return 20;
  auto restored_identity = application->native_world()->node_identity(camera_before);
  gneiss::engine::math::camera_settings restored_camera;
  gneiss::scene_node_id restored_parent;
  if (!restored_identity || restored_identity->name != "Camera" ||
      application->native_world()
          ->entity_get_camera(camera_entity_before, restored_camera)
          .failed() ||
      gneiss::test_support::assign_value(
          application->native_world()->find(gneiss::entity_id{(old_child).get()}).parent(),
          restored_parent)
          .failed() ||
      restored_parent != camera_before)
    return 21;

  const auto retired_child = old_child;
  write_scene(scene_path, "[]");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, uri, false) !=
      gneiss::engine::error::success) {
    return 2;
  }
  gneiss::scene_node_id camera_after;
  gneiss::scene_node_id new_child;
  gneiss::entity_id camera_entity_after;
  std::uint64_t count = 0U;
  if (gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(scene, camera_uuid),
          camera_after) != gneiss::engine::error::success ||
      camera_after != camera_before ||
      application->native_world()->node_get_entity(camera_after, camera_entity_after) !=
          gneiss::engine::error::success ||
      camera_entity_after != camera_entity_before ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(scene, new_child_uuid),
          new_child) != gneiss::engine::error::success ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(scene, old_child_uuid),
          old_child) != gneiss::engine::error::not_found ||
      gneiss::test_support::assign_value(
          application->native_world()->find(gneiss::entity_id{(retired_child).get()}).parent(),
          camera_after) != gneiss::engine::error::invalid_handle ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->get_node_count(scene), count) !=
          gneiss::engine::error::success ||
      count != 2U) {
    return 3;
  }

  write_scene(
      scene_path,
      R"([{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/missing.mesh.json","material":"asset://materials/triangle.material.json","uuid":"6ac36c36-95c2-4975-9b2a-4fbcc4c7bca1"}}])");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, uri, false) !=
          gneiss::engine::error::success ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(scene, camera_uuid),
          camera_after) != gneiss::engine::error::success ||
      camera_after != camera_before ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(scene, new_child_uuid),
          new_child) != gneiss::engine::error::success) {
    return 4;
  }

  if (visual_failures != 1)
    return 22;
  auto binding =
      gneiss::engine::capture_mesh_binding(*application->native_world(), scene, new_child);
  if (!binding || !binding->mesh || !binding->material ||
      !application->assets().find<gneiss::engine::material>(
          "asset://materials/triangle.material.json"))
    return 23;
  binding = gneiss::engine::error::not_found;

  // 缺失材质也不阻断结构回执；回执到达时视觉加载仍未结束。
  write_scene(
      scene_path,
      R"([{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/missing.mesh.json","material":"asset://materials/missing.material.json","uuid":"6ac36c36-95c2-4975-9b2a-4fbcc4c7bca1"}}])");
  if (application->request_scene_reload(scene, uri, false).failed())
    return 24;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    (void)scheduler.run_ready();
    auto completed = application->poll_scene_reload();
    if (!completed)
      return 25;
    if (*completed) {
      if ((**completed).result.failed() || !application->visual_assets_loading())
        return 26;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return 27;
  }
  while (application->visual_assets_loading()) {
    if (std::chrono::steady_clock::now() >= deadline)
      return 28;
    (void)scheduler.run_ready();
    application->advance_scene_load();
  }
  if (visual_failures != 3)
    return 29;
  // 描述本身损坏仍失败，不能把解析失败解释为缺失视觉内容。
  std::ofstream(scene_path) << "broken";
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, scene, uri, false).ok())
    return 30;

  const bool destroyed = gneiss::test_support::scene_access{*application}->unload(scene) ==
                             gneiss::engine::error::success &&
                         application->shutdown() == gneiss::engine::error::success;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 5;
} catch (...) {
  return 99;
}
