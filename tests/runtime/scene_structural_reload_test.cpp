// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_scene_reload.hpp"
#include "../support/logging.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/function/scene/world.hpp>

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace {

const gneiss::guid scene_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000001");
const gneiss::guid camera_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000002");
const gneiss::guid old_child_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000003");
const gneiss::guid new_child_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000004");

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
  gneiss::tasks::scheduler scheduler({.mode = gneiss::tasks::execution_mode::cooperative});
  config.executor = &scheduler;
  auto created = gneiss::runtime::application::create(config);
  if (!created)
    return 1;
  auto application = *created;
  auto created_world = gneiss::create_native();
  if (!created_world)
    return 1;
  auto& world = **created_world;
  gneiss::host::scene_open_result loaded;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  if (gneiss::test_support::activate_scene_for_test(*application, world, scheduler, uri, loaded)
          .failed())
    return 1;
  auto scene = loaded.scene;
  // 来源 UUID 只查询本次实例化回执；运行对象不保存文档身份。
  const auto find_object = [&](gneiss::guid source) {
    const auto found = std::ranges::find_if(loaded.objects, [&](const auto& binding) {
      return binding.object == source && !binding.source;
    });
    return found == loaded.objects.end() ? gneiss::game_object{} : world.find(found->entity);
  };
  const auto camera_before = find_object(camera_uuid);
  const auto old_child = find_object(old_child_uuid);
  if (!camera_before || !old_child)
    return 1;

  // 新的加载不能覆盖调用方保留的旧场景基线。
  gneiss::host::scene_open_result second;
  if (gneiss::test_support::activate_scene_for_test(*application, world, scheduler, uri, second)
          .failed() ||
      !loaded.reload_state || !second.reload_state || loaded.reload_state == second.reload_state)
    return 21;
  gneiss::host::runtime_scene_loader reloader(*application, world);
  if (reloader.reload({}, uri, false) != gneiss::error::invalid_state)
    return 22;
  auto other = gneiss::runtime::application::create(config);
  if (!other)
    return 23;
  auto other_world = gneiss::create_native();
  if (!other_world)
    return 1;
  gneiss::host::runtime_scene_loader other_loader(**other, **other_world);
  if (other_loader.reload(loaded.reload_state, uri, false) != gneiss::error::invalid_state)
    return 23;

  // 直接修改 World 后重载同一文件，差异必须基于当前对象而非加载时描述。
  camera_before.set_name("Edited");
  if (camera_before.remove_component<gneiss::camera_component>().failed() ||
      old_child.reparent({}).failed() ||
      gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false, &loaded.objects)
          .failed())
    return 20;
  const auto* restored_camera = camera_before.try_component<gneiss::camera_component>();
  auto restored_parent = old_child.parent();
  if (camera_before.name() != "Camera" || !restored_camera || !restored_parent ||
      restored_parent != camera_before)
    return 21;

  const auto retired_child = old_child;
  write_scene(scene_path, "[]");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false,
                                                  &loaded.objects) != gneiss::error::success) {
    return 2;
  }
  auto camera_after = find_object(camera_uuid);
  auto new_child = find_object(new_child_uuid);
  auto count = scene.size();
  if (!camera_after || camera_after != camera_before || !new_child || find_object(old_child_uuid) ||
      retired_child.is_valid() || count != 2U)
    return 3;

  write_scene(
      scene_path,
      R"([{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://models/missing.mesh.json","material":"asset://materials/triangle.material.json","uuid":"6ac36c36-95c2-4975-9b2a-4fbcc4c7bca1"}}])");
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false, &loaded.objects)
          .failed())
    return 4;
  camera_after = find_object(camera_uuid);
  new_child = find_object(new_child_uuid);
  if (!camera_after || camera_after != camera_before || !new_child)
    return 4;

  if (!received_failures(1))
    return 22;
  const auto* binding = new_child.try_component<gneiss::mesh_component>();
  if (!binding || !binding->mesh || !binding->material ||
      !application->assets().find<gneiss::material>("asset://materials/triangle.material.json"))
    return 23;
  binding = nullptr; // 后续重载可能替换组件，不跨结构修改保留借用。

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
    (void)application->step();
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
    (void)application->step();
    (void)reloader.advance();
  }
  (void)application->step();
  if (!received_failures(3))
    return 29;
  // 描述本身损坏仍失败，不能把解析失败解释为缺失视觉内容。
  std::ofstream(scene_path) << "broken";
  if (gneiss::test_support::reload_scene_for_test(*application, scheduler, loaded.reload_state, uri,
                                                  false, &loaded.objects)
          .ok())
    return 30;

  scene.unload();
  const bool destroyed =
      reloader.reload(loaded.reload_state, uri, false) == gneiss::error::invalid_handle &&
      world.object(second.objects.front().entity).has_value();
  second.scene.unload();
  application->request_exit();
  const bool stopped = application->run().ok();
  std::filesystem::remove_all(root);
  return destroyed && stopped ? 0 : 5;
} catch (...) {
  return 99;
}
