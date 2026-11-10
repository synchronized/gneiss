// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/scope_exit.hpp"
#include "engine/core/fatal.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/component_identity.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/asset_file.hpp"
#include "runtime/application.hpp"
#include <algorithm>
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
                gneiss::scene_instance_id& scene,
                std::vector<gneiss::engine::scene_object_binding>& bindings,
                gneiss::engine::scene_description* description = nullptr) {
  gneiss::runtime::application_configuration config;
  config.asset_root = root;
  auto created = gneiss::runtime::application::create(config);
  if (!created)
    return false;
  app = std::move(*created);
  if (app->attach_task_executor(scheduler).failed())
    return false;
  auto submitted = app->request_scene(scene_uri, 1, 1);
  if (!submitted)
    return false;
  const auto request = *submitted;
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
  auto activated = app->activate_scene(request);
  if (!activated)
    return false;
  completion = std::move(*activated);
  scene = completion.scene;
  bindings = std::move(completion.objects);
  if (description)
    *description = *completion.content->description;
  return true;
}
bool target_for(gneiss::runtime::application& app,
                std::span<const gneiss::engine::scene_object_binding> bindings,
                gneiss::entity_id& target) {
  auto found = std::ranges::find_if(bindings, [](const auto& binding) {
    return binding.object == camera_uuid && !binding.source;
  });
  if (found == bindings.end() || !app.native_world()->find(found->entity))
    return false;
  target = found->entity;
  return true;
}
}
int main() try {
  gneiss::engine::install_fatal_handler();
  if (gneiss::engine::initialize_meta().failed())
    return 99;
  auto count = gneiss::engine::meta::type_count();
  if (!count)
    return 1;
  for (std::uint32_t i = 0; i < *count; ++i) {
    auto type = gneiss::engine::meta::type_at(i);
    if (!type)
      return 1;
    std::printf("类型 %.*s\n", static_cast<int>(type->name().size()), type->name().data());
    auto properties = type->properties();
    for (const auto& field : properties)
      std::printf("  字段 %u: %.*s\n", field.id().get(), static_cast<int>(field.name().size()),
                  field.name().data());
  }
  gneiss::engine::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::scene_instance_id scene{};
  std::vector<gneiss::engine::scene_object_binding> bindings;
  example::scope_exit close{[&] {
    if (app) {
      if (scene) {
        auto instance = gneiss::engine::scene_instance::borrow(*app->native_world(), scene);
        if (instance)
          (void)instance->unload();
      }
      (void)app->shutdown();
    }
  }};
  gneiss::engine::scene_description description;
  if (!open_scene(scheduler, GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT, app, scene, bindings,
                  &description))
    return 2;
  gneiss::entity_id target{};
  if (!target_for(*app, bindings, target))
    return 3;
  gneiss::engine::value value{.payload = gneiss::engine::math::vector3{2, 1, 4}};
  if (gneiss::engine::write_component_property(*app->native_world(), target.get(),
                                               gneiss::engine::math::transform::type_guid,
                                               gneiss::engine::meta::property_id{1}, value)
          .failed())
    return 3;
  auto node = app->native_world()->entity_node(target);
  if (!node)
    return 3;
  auto components = gneiss::engine::capture_components(*app->native_world(), *node);
  if (!components)
    return 3;
  auto camera = components->find(gneiss::engine::camera_description::type_guid);
  if (!camera)
    return 3;
  auto changed = gneiss::engine::modify_component(
      *camera, [](gneiss::engine::meta::instance root) -> gneiss::engine::error {
        auto settings = root.member(gneiss::engine::meta::property_id{1});
        if (!settings)
          return settings.error();
        auto near = settings->member(gneiss::engine::meta::property_id{2});
        return near ? near->assign(gneiss::engine::value{0.25F}) : near.error();
      });
  if (!changed)
    return 3;
  components->set(*changed);
  auto object = app->native_world()->object(target);
  if (!object || gneiss::engine::install_components({*object}, *components).failed())
    return 3;
  // 保存以加载时的描述为基础，只写回本示例明确修改的相机属性。
  auto author = std::ranges::find(description.objects, camera_uuid,
                                  &gneiss::engine::game_object_description::uuid);
  if (author == description.objects.end())
    return 4;
  auto local = app->native_world()->object(target);
  auto transform = local ? local->local_transform()
                         : gneiss::engine::result<gneiss::engine::math::transform>{local.error()};
  if (!transform)
    return 4;
  author->transform = *transform;
  author->components = *components;
  auto serialized = gneiss::engine::asset_file::serialize(description);
  auto instance = gneiss::engine::scene_instance::borrow(*app->native_world(), scene);
  if (!serialized || !instance || instance->unload().failed())
    return 4;
  auto json = std::move(*serialized);
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
  if (!open_scene(scheduler, output_root.generic_string(), app, scene, bindings) ||
      !target_for(*app, bindings, target))
    return 6;
  auto translation = gneiss::engine::read_component_property(
      *app->native_world(), target.get(), gneiss::engine::math::transform::type_guid,
      gneiss::engine::meta::property_id{1});
  if (!translation || translation->kind() != gneiss::engine::value_kind::vec3 ||
      std::abs(std::get<gneiss::engine::math::vector3>(translation->payload).x - 2) > 0.0001F)
    return 6;
  auto runtime_camera =
      app->native_world()->find(target).read_component<gneiss::engine::camera_component>();
  if (!runtime_camera || std::abs(runtime_camera->value.near - 0.25F) > 0.0001F)
    return 6;
  instance = gneiss::engine::scene_instance::borrow(*app->native_world(), scene);
  if (!instance || instance->unload().failed())
    return 6;
  scene = {};
  if (app->shutdown().failed())
    return 6;
  app.reset();
  std::filesystem::remove_all(output_root);
  std::puts("属性修改、保存与重新加载成功");
  return 0;
} catch (...) {
  gneiss::engine::fatal_current_exception();
}
