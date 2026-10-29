// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/scope_exit.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
namespace {
constexpr std::string_view scene_uri = "asset://scenes/property.scene.json";
const auto camera_uuid = *gneiss::engine::guid::parse("37cff772-2e8d-4bc7-9ed2-f94435926d4e");
bool open_scene(gneiss::engine::task_scheduler& scheduler, std::string_view root,
                std::shared_ptr<gneiss::runtime::application>& app,
                gneiss::scene_instance_id& scene) {
  gneiss::runtime::application_configuration config;
  config.asset_root = root;
  if (gneiss::runtime::application::create(config, app).failed() ||
      app->attach_task_executor(scheduler).failed())
    return false;
  std::uint64_t request{};
  if (app->request_scene(scene_uri, 1, 1, request).failed())
    return false;
  gneiss::runtime::scene_load_completion completion;
  // 命令行示例的宿主循环；解码与 I/O 都从异步请求取得。
  for (;;) {
    (void)scheduler.run_ready();
    bool finished = false;
    if (app->run(1).failed() || app->poll_scene(completion, finished).failed() ||
        (finished && completion.result.failed()))
      return false;
    gneiss::runtime::scene_load_progress progress;
    if (app->scene_progress(progress) &&
        progress.phase == gneiss::runtime::scene_load_phase::ready && app->can_activate_scene())
      break;
  }
  if (app->activate_scene(request, completion).failed())
    return false;
  scene = completion.scene;
  return true;
}
bool target_for(gneiss::runtime::application& app, gneiss::scene_instance_id scene,
                gneiss::entity_id& target) {
  gneiss::scene_node_id node;
  gneiss::entity_id entity;
  if (app.scenes()->find_node(scene, camera_uuid, node).failed() ||
      app.native_world()->node_get_entity(node, entity).failed())
    return false;
  target = entity;
  return true;
}
}
int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  auto created_registry = gneiss::engine::meta::default_registry();
  if (!created_registry)
    return 1;
  auto registry = std::move(*created_registry);
  auto count = gneiss::engine::meta::type_count(registry);
  if (!count)
    return 1;
  for (std::uint32_t i = 0; i < *count; ++i) {
    auto type = gneiss::engine::meta::describe_type_at(registry, i);
    if (!type)
      return 1;
    std::printf("类型 %.*s\n", static_cast<int>(type->name.size()), type->name.data());
    for (const auto& field : type->properties)
      std::printf("  字段 %u: %.*s\n", field.id.get(), static_cast<int>(field.name.size()),
                  field.name.data());
  }
  gneiss::engine::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::scene_instance_id scene{};
  example::scope_exit close{[&] {
    if (app) {
      if (scene)
        (void)app->scenes()->unload(scene);
      (void)app->shutdown();
    }
  }};
  if (!open_scene(scheduler, GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT, app, scene))
    return 2;
  gneiss::entity_id target{};
  if (!target_for(*app, scene, target))
    return 3;
  gneiss::engine::value value{.payload = gneiss::engine::math::vector3{2, 1, 4}};
  if (gneiss::engine::write_component_property(registry, *app->native_world(), target.get(),
                                               gneiss::engine::transform_property_type(),
                                               gneiss::engine::meta::property_id{1}, value)
          .failed())
    return 3;
  value.payload = 0.25F;
  if (gneiss::engine::write_component_property(registry, *app->native_world(), target.get(),
                                               gneiss::engine::camera_property_type(),
                                               gneiss::engine::meta::property_id{2}, value)
          .failed())
    return 3;
  std::string json;
  if (app->scenes()->serialize(scene, json).failed() || app->scenes()->unload(scene).failed())
    return 4;
  scene = {};
  if (app->shutdown().failed())
    return 4;
  app.reset();
  const auto output_root =
      std::filesystem::temp_directory_path() /
      ("gneiss-property-inspector-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto path = output_root / "scenes/property.scene.json";
  std::filesystem::create_directories(path.parent_path());
  {
    std::ofstream out(path, std::ios::binary);
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!out)
      return 5;
  }
  if (!open_scene(scheduler, output_root.generic_string(), app, scene) ||
      !target_for(*app, scene, target))
    return 6;
  if (gneiss::engine::read_component_property(registry, *app->native_world(), target.get(),
                                              gneiss::engine::transform_property_type(),
                                              gneiss::engine::meta::property_id{1}, value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::vec3 ||
      std::abs(std::get<gneiss::engine::math::vector3>(value.payload).x - 2) > 0.0001F)
    return 6;
  if (gneiss::engine::read_component_property(registry, *app->native_world(), target.get(),
                                              gneiss::engine::camera_property_type(),
                                              gneiss::engine::meta::property_id{2}, value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::float32 ||
      std::abs(std::get<float>(value.payload) - 0.25F) > 0.0001F)
    return 6;
  if (app->scenes()->unload(scene).failed())
    return 6;
  scene = {};
  if (app->shutdown().failed())
    return 6;
  app.reset();
  std::filesystem::remove_all(output_root);
  std::puts("属性修改、保存与重新加载成功");
  return 0;
} catch (...) {
  return 99;
}
