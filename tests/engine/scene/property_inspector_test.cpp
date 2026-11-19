// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../../examples/support/scene_activation.hpp"
#include "../../../examples/support/scope_exit.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/core/meta/capabilities/object.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/scene_instance.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/asset_file.hpp>
#include <memory>
#include <string>
#include <string_view>
namespace {
constexpr std::string_view scene_uri = "asset://scenes/property.scene.json";
const auto camera_uuid = *gneiss::guid::parse("37cff772-2e8d-4bc7-9ed2-f94435926d4e");
bool open_scene(gneiss::tasks::task_scheduler& scheduler, std::string_view root,
                std::shared_ptr<gneiss::runtime::application>& app, gneiss::world& world,
                gneiss::scene_instance& scene, std::vector<gneiss::scene_object_binding>& bindings,
                gneiss::scene_description* description = nullptr) {
  gneiss::runtime::application_configuration config;
  config.asset_root = root;
  config.executor = &scheduler;
  auto created = gneiss::runtime::application::create(config);
  if (!created)
    return false;
  app = std::move(*created);
  auto submitted = app->assets().load<gneiss::scene>(scene_uri);
  if (!submitted)
    return false;
  auto request = std::move(*submitted);
  // 命令行示例由宿主循环处理文件完成，不使用同步加载包装。
  while (request.status() == gneiss::error::not_ready) {
    (void)scheduler.run_ready();
    if (app->run(1).failed())
      return false;
  }
  auto content = request.snapshot();
  auto loaded = request.take();
  if (!loaded || !content)
    return false;
  auto activated = example::activate_scene(*app, world, *content);
  if (!activated)
    return false;
  scene = activated->instance.instance;
  bindings = std::move(activated->instance.objects);
  while (activated->visuals && !activated->visuals.ready()) {
    (void)scheduler.run_ready();
    if (app->run(1).failed())
      return false;
  }
  example::report_visuals(*activated);
  if (description)
    *description = *content->description;
  return true;
}
bool target_for(gneiss::world& world, std::span<const gneiss::scene_object_binding> bindings,
                gneiss::entity_id& target) {
  auto found = std::ranges::find_if(bindings, [](const auto& binding) {
    return binding.object == camera_uuid && !binding.source;
  });
  if (found == bindings.end() || !world.find(found->entity))
    return false;
  target = found->entity;
  return true;
}
}
int main() try {
  gneiss::install_fatal_handler();
  if (gneiss::initialize_meta().failed())
    return 99;
  auto types = gneiss::meta::registered_types();
  if (!types)
    return 1;
  for (const auto type : *types) {
    std::printf("类型 %.*s\n", static_cast<int>(type.name().size()), type.name().data());
    auto properties = type.template capability<gneiss::meta::cap::object>().properties();
    for (const auto& field : properties)
      std::printf("  字段 %u: %.*s\n", field.id().get(), static_cast<int>(field.name().size()),
                  field.name().data());
  }
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> app;
  auto world = gneiss::create_native();
  if (!world)
    return 2;
  gneiss::scene_instance scene{};
  std::vector<gneiss::scene_object_binding> bindings;
  example::scope_exit close{[&] {
    if (app) {
      if (scene.is_valid())
        scene.unload();
      (void)app->shutdown();
    }
  }};
  gneiss::scene_description description;
  if (!open_scene(scheduler, GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT, app, **world, scene, bindings,
                  &description))
    return 2;
  gneiss::entity_id target{};
  if (!target_for(**world, bindings, target))
    return 3;
  gneiss::value value{.payload = gneiss::math::vector3{2, 1, 4}};
  if (gneiss::write_component_property(**world, target.get(), gneiss::math::transform::type_guid,
                                       gneiss::property_id{1}, value)
          .failed())
    return 3;
  auto node = (*world)->object(target);
  if (!node)
    return 3;
  auto components = gneiss::capture_components(**world, node->entity());
  if (!components)
    return 3;
  auto camera = components->find(gneiss::camera_description::type_guid);
  if (!camera)
    return 3;
  auto changed =
      gneiss::modify_component(*camera, [](gneiss::meta::instance root) -> gneiss::error {
        auto settings =
            root.template capability<gneiss::meta::cap::object>()->member(gneiss::property_id{1});
        if (!settings)
          return settings.error();
        auto near = settings->template capability<gneiss::meta::cap::object>()->member(
            gneiss::property_id{2});
        return near ? near->assign(gneiss::value{0.25F}) : near.error();
      });
  if (!changed)
    return 3;
  components->set(*changed);
  auto object = (*world)->object(target);
  if (!object || gneiss::replace_components({*object}, *components).failed())
    return 3;
  // 保存以加载时的描述为基础，只写回本示例明确修改的相机属性。
  auto author =
      std::ranges::find(description.objects, camera_uuid, &gneiss::game_object_description::uuid);
  if (author == description.objects.end())
    return 4;
  auto local = (*world)->object(target);
  if (!local)
    return 4;
  author->transform = local->local_transform();
  author->components = *components;
  auto serialized = gneiss::asset_file::serialize(description);
  if (!serialized || !scene.is_valid())
    return 4;
  scene.unload();
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
  if (!open_scene(scheduler, output_root.generic_string(), app, **world, scene, bindings) ||
      !target_for(**world, bindings, target))
    return 6;
  auto translation = gneiss::read_component_property(
      **world, target.get(), gneiss::math::transform::type_guid, gneiss::property_id{1});
  if (!translation || translation->kind() != gneiss::value_kind::vec3 ||
      std::abs(std::get<gneiss::math::vector3>(translation->payload).x - 2) > 0.0001F)
    return 6;
  auto runtime_camera = (*world)->find(target).try_component<gneiss::camera_component>();
  if (!runtime_camera || std::abs(runtime_camera->settings.near_plane - 0.25F) > 0.0001F)
    return 6;
  if (!scene.is_valid())
    return 6;
  scene.unload();
  scene = {};
  if (app->shutdown().failed())
    return 6;
  app.reset();
  std::filesystem::remove_all(output_root);
  std::puts("属性修改、保存与重新加载成功");
  return 0;
} catch (...) {
  gneiss::fatal_current_exception();
}
