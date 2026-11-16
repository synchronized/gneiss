// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_application.hpp"
#include "../../support/async_scene_reload.hpp"
#include "../../support/logging.hpp"
#include "../../support/mesh_objects.hpp"
#include "../../support/scene_access.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/function/scene/world.hpp>

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace {

const gneiss::guid scene_uuid =
    *gneiss::guid::parse("00000000-0000-4000-8000-000000000001");
const gneiss::guid camera_uuid =
    *gneiss::guid::parse("00000000-0000-4000-8000-000000000002");
const gneiss::guid old_child_uuid =
    *gneiss::guid::parse("00000000-0000-4000-8000-000000000003");
const gneiss::guid new_child_uuid =
    *gneiss::guid::parse("00000000-0000-4000-8000-000000000004");

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
  std::mutex log_mutex;
  std::condition_variable log_received;
  unsigned visual_failures{};
  gneiss::test_support::log_capture logs{[&](const auto& event) {
    if (event.message.category == "scene_assets") {
      const std::lock_guard lock(log_mutex);
      ++visual_failures;
      log_received.notify_all();
    }
  }};
  // 日志在独立线程分发；视觉批次完成不等于日志回调已执行。
  const auto received_failures = [&](unsigned expected) {
    std::unique_lock lock(log_mutex);
    return log_received.wait_for(lock, std::chrono::seconds(5), [&] {
      return visual_failures >= expected;
    }) && visual_failures == expected;
  };
  gneiss::tasks::task_scheduler scheduler({.mode = gneiss::tasks::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance scene = {};
  gneiss::host::scene_open_result loaded;
  gneiss::entity_id camera_before;
  gneiss::entity_id old_child;
  gneiss::entity_id camera_entity_before;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(config),
                                         application) != gneiss::error::success ||
      gneiss::test_support::activate_scene_for_test(*application, scheduler, uri, loaded) !=
          gneiss::error::success ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(loaded.objects, camera_uuid),
          camera_before) != gneiss::error::success ||
      gneiss::test_support::take_value(gneiss::test_support::scene_access{*application}->find_node(
                                           loaded.objects, old_child_uuid),
                                       old_child) != gneiss::error::success ||
      gneiss::test_support::assign_value(
          application->native_world()->object(gneiss::entity_id{(camera_before).get()}),
          camera_entity_before) != gneiss::error::success) {
    return 1;
  }

  scene = loaded.scene;
  // 新的加载不能覆盖调用方保留的旧场景基线。
  gneiss::host::scene_open_result second;
  if (gneiss::test_support::activate_scene_for_test(*application, scheduler, uri, second)
          .failed() ||
      !loaded.reload_state || !second.reload_state || loaded.reload_state == second.reload_state)
    return 21;
  gneiss::host::runtime_scene_loader reloader(*application);
  if (reloader.reload({}, uri, false) != gneiss::error::invalid_state)
    return 22;
  auto other = gneiss::runtime::application::create(config);
  if (!other)
    return 23;
  gneiss::host::runtime_scene_loader other_loader(**other);
  if (other_loader.reload(loaded.reload_state, uri, false) != gneiss::error::invalid_state)
    return 23;

  // 直接修改 World 后重载同一文件，差异必须基于当前对象而非加载时描述。
  if (application->native_world()
          ->find(gneiss::entity_id{(camera_before).get()})
          .set_name("Edited")
          .failed() ||
      application->native_world()
          ->find(camera_entity_before)
          .remove_component<gneiss::camera_component>()
          .failed() ||
      application->native_world()
          ->find(gneiss::entity_id{(old_child).get()})
          .reparent(gneiss::game_object{})
          .failed() ||
      gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false, &loaded.objects)
          .failed())
    return 20;
  auto restored_identity =
      application->native_world()->find(gneiss::entity_id{(camera_before).get()}).name();
  gneiss::math::camera_settings restored_camera;
  gneiss::entity_id restored_parent;
  if (restored_identity != "Camera" ||
      gneiss::test_support::read_component_value(
          application->native_world()
              ->find(camera_entity_before)
              .try_component<gneiss::camera_component>(),
          restored_camera)
          .failed() ||
      gneiss::test_support::assign_value(
          application->native_world()->find(gneiss::entity_id{(old_child).get()}).parent(),
          restored_parent)
          .failed() ||
      restored_parent != camera_before)
    return 21;

  const auto retired_child = old_child;
  write_scene(scene_path, "[]");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false, &loaded.objects) !=
      gneiss::error::success) {
    return 2;
  }
  gneiss::entity_id camera_after;
  gneiss::entity_id new_child;
  gneiss::entity_id camera_entity_after;
  std::uint64_t count = 0U;
  if (gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(loaded.objects, camera_uuid),
          camera_after) != gneiss::error::success ||
      camera_after != camera_before ||
      gneiss::test_support::assign_value(
          application->native_world()->object(gneiss::entity_id{(camera_after).get()}),
          camera_entity_after) != gneiss::error::success ||
      camera_entity_after != camera_entity_before ||
      gneiss::test_support::take_value(gneiss::test_support::scene_access{*application}->find_node(
                                           loaded.objects, new_child_uuid),
                                       new_child) != gneiss::error::success ||
      gneiss::test_support::take_value(gneiss::test_support::scene_access{*application}->find_node(
                                           loaded.objects, old_child_uuid),
                                       old_child) != gneiss::error::not_found ||
      gneiss::test_support::assign_value(
          application->native_world()->find(gneiss::entity_id{(retired_child).get()}).parent(),
          camera_after) != gneiss::error::invalid_handle ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->get_node_count(scene), count) !=
          gneiss::error::success ||
      count != 2U) {
    return 3;
  }

  write_scene(
      scene_path,
      R"([{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/missing.mesh.json","material":"asset://materials/triangle.material.json","uuid":"6ac36c36-95c2-4975-9b2a-4fbcc4c7bca1"}}])");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false, &loaded.objects) !=
          gneiss::error::success ||
      gneiss::test_support::take_value(
          gneiss::test_support::scene_access{*application}->find_node(loaded.objects, camera_uuid),
          camera_after) != gneiss::error::success ||
      camera_after != camera_before ||
      gneiss::test_support::take_value(gneiss::test_support::scene_access{*application}->find_node(
                                           loaded.objects, new_child_uuid),
                                       new_child) != gneiss::error::success) {
    return 4;
  }

  if (!received_failures(1))
    return 22;
  auto binding = gneiss::test_support::read_mesh(*application->native_world(), scene, new_child);
  if (!binding || !binding->mesh || !binding->material ||
      !application->assets().find<gneiss::material>(
          "asset://materials/triangle.material.json"))
    return 23;
  binding = gneiss::error::not_found;

  // 缺失材质也不阻断结构回执；回执到达时视觉加载仍未结束。
  write_scene(
      scene_path,
      R"([{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/missing.mesh.json","material":"asset://materials/missing.material.json","uuid":"6ac36c36-95c2-4975-9b2a-4fbcc4c7bca1"}}])");
  auto reload_request = reloader.reload(loaded.reload_state, uri, false);
  if (reload_request.failed())
    return 24;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    (void)scheduler.run_ready();
    (void)application->run(1);
    (void)reloader.advance();
    auto completed = reloader.take_reload();
    if (!completed && completed.error() != gneiss::error::not_ready)
      return 25;
    if (completed) {
      if (completed->result.failed() || !reloader.visual_loading())
        return 26;
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return 27;
  }
  while (reloader.visual_loading()) {
    if (std::chrono::steady_clock::now() >= deadline)
      return 28;
    (void)scheduler.run_ready();
    (void)application->run(1);
    (void)reloader.advance();
  }
  (void)application->run(1);
  if (!received_failures(3))
    return 29;
  // 描述本身损坏仍失败，不能把解析失败解释为缺失视觉内容。
  std::ofstream(scene_path) << "broken";
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false, &loaded.objects)
          .ok())
    return 30;

  const bool destroyed =
      gneiss::test_support::scene_access{*application}->unload(scene) ==
          gneiss::error::success &&
      reloader.reload(loaded.reload_state, uri, false) == gneiss::error::invalid_handle &&
      application->native_world()->object(second.objects.front().entity).has_value() &&
      application->shutdown() == gneiss::error::success;
  std::filesystem::remove_all(root);
  return destroyed ? 0 : 5;
} catch (...) {
  return 99;
}
