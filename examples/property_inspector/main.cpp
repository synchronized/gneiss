// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/scope_exit.hpp"
#include "engine/core/reflection/property_registry.hpp"
#include "engine/core/rid.hpp"
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
constexpr std::string_view camera_uuid = "37cff772-2e8d-4bc7-9ed2-f94435926d4e";
bool open_scene(std::string_view root, std::shared_ptr<gneiss::runtime::application>& app,
                gneiss::engine::rid& scene) {
  gneiss::runtime::application_configuration config;
  config.asset_root = root;
  return gneiss::runtime::application::create(config, app) == gneiss::engine::result::success &&
         app->scenes()->load(scene_uri, &scene) == gneiss::engine::result::success;
}
bool target_for(gneiss::runtime::application& app, gneiss::engine::rid scene,
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
  gneiss::engine::registry_ref registry;
  if (gneiss::engine::create_property_registry(registry).failed())
    return 1;
  std::uint32_t count{};
  if (gneiss::engine::type_count(registry, count).failed())
    return 1;
  for (std::uint32_t i = 0; i < count; ++i) {
    gneiss::type_info type;
    if (gneiss::engine::describe_type_at(registry, i, type).failed())
      return 1;
    std::printf("类型 %.*s\n", static_cast<int>(type.name.size()), type.name.data());
    for (const auto& field : type.fields)
      std::printf("  字段 %u: %.*s\n", field.id.get(), static_cast<int>(field.name.size()),
                  field.name.data());
  }
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::engine::rid scene{};
  example::scope_exit close{[&] {
    if (app) {
      if (scene)
        (void)app->scenes()->unload(scene);
      (void)app->shutdown();
    }
  }};
  if (!open_scene(GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT, app, scene))
    return 2;
  gneiss::entity_id target{};
  if (!target_for(*app, scene, target))
    return 3;
  gneiss::engine::property_value value{.payload = gneiss::property_vec3{2, 1, 4}};
  if (gneiss::engine::write_component_property(registry, app->world(), target.get(),
                                               gneiss::engine::transform_property_type(),
                                               gneiss::field_id{1}, value)
          .failed())
    return 3;
  value.payload = 0.25F;
  if (gneiss::engine::write_component_property(registry, app->world(), target.get(),
                                               gneiss::engine::camera_property_type(),
                                               gneiss::field_id{2}, value)
          .failed())
    return 3;
  std::string json;
  if (app->scenes()->serialize(scene, json).failed() || app->scenes()->unload(scene).failed())
    return 4;
  scene = 0;
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
  if (!open_scene(output_root.generic_string(), app, scene) || !target_for(*app, scene, target))
    return 6;
  if (gneiss::engine::read_component_property(registry, app->world(), target.get(),
                                              gneiss::engine::transform_property_type(),
                                              gneiss::field_id{1}, value)
          .failed() ||
      value.kind() != gneiss::property_kind::vec3 ||
      std::abs(std::get<gneiss::property_vec3>(value.payload).x - 2) > 0.0001F)
    return 6;
  if (gneiss::engine::read_component_property(registry, app->world(), target.get(),
                                              gneiss::engine::camera_property_type(),
                                              gneiss::field_id{2}, value)
          .failed() ||
      value.kind() != gneiss::property_kind::float32 ||
      std::abs(std::get<float>(value.payload) - 0.25F) > 0.0001F)
    return 6;
  if (app->scenes()->unload(scene).failed())
    return 6;
  scene = 0;
  if (app->shutdown().failed())
    return 6;
  app.reset();
  std::filesystem::remove_all(output_root);
  std::puts("属性修改、保存与重新加载成功");
  return 0;
} catch (...) {
  return 99;
}
