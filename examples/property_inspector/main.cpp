// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/scope_exit.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gneiss/gneiss.h>
#include <string>
#include <string_view>
namespace {
constexpr std::string_view scene_uri = "asset://scenes/property.scene.json";
constexpr std::string_view camera_uuid = "37cff772-2e8d-4bc7-9ed2-f94435926d4e";
bool open_scene(std::string_view root, gneiss_application& app, gneiss_scene_instance& scene) {
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<uint32_t>(root.size());
  return gneiss_application_create(&desc, &app) == GNEISS_SUCCESS &&
         gneiss_scene_instance_load(app, scene_uri.data(), scene_uri.size(), &scene) ==
             GNEISS_SUCCESS;
}
bool target_for(gneiss_application app, gneiss_scene_instance scene,
                gneiss_property_target& target) {
  gneiss_world world{};
  gneiss_scene_node_id node{};
  gneiss_entity_id entity{};
  if (gneiss_application_get_world(app, &world) != GNEISS_SUCCESS ||
      gneiss_scene_instance_find_node(app, scene, camera_uuid.data(), camera_uuid.size(), &node) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_get_entity(world, node, &entity) != GNEISS_SUCCESS)
    return false;
  target = {world, entity};
  return true;
}
}
int main() try {
  gneiss_type_registry registry{};
  example::scope_exit release_registry{[&] {
    if (registry)
      (void)gneiss_type_registry_destroy(registry);
  }};
  if (gneiss_type_registry_create(&registry) != GNEISS_SUCCESS ||
      gneiss_world_register_reflection(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS)
    return 1;
  uint32_t count{};
  if (gneiss_type_registry_type_count(registry, &count) != GNEISS_SUCCESS)
    return 1;
  for (uint32_t i = 0; i < count; ++i) {
    gneiss_type_info type = GNEISS_TYPE_INFO_INIT;
    if (gneiss_type_registry_type_at(registry, i, &type) != GNEISS_SUCCESS)
      return 1;
    std::printf("类型 %.*s\n", static_cast<int>(type.name_length), type.name);
    for (uint32_t j = 0; j < type.field_count; ++j) {
      const auto& field = type.fields[j];
      std::printf("  字段 %u: %.*s\n", field.id, static_cast<int>(field.name_length), field.name);
    }
  }
  gneiss_application app{};
  gneiss_scene_instance scene{};
  example::scope_exit close{[&] {
    if (scene)
      (void)gneiss_scene_instance_unload(app, scene);
    if (app)
      (void)gneiss_application_destroy(app);
  }};
  if (!open_scene(GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT, app, scene))
    return 2;
  gneiss_property_target target{};
  if (!target_for(app, scene, target))
    return 3;
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  value.kind = GNEISS_PROPERTY_KIND_VEC3;
  value.payload.vec3_value = {2, 1, 4};
  if (gneiss_type_registry_set_property(registry, gneiss_transform_type_id(), 1, target, &value) !=
      GNEISS_SUCCESS)
    return 3;
  value.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  value.payload.float32_value = 0.25F;
  if (gneiss_type_registry_set_property(registry, gneiss_camera_type_id(), 2, target, &value) !=
      GNEISS_SUCCESS)
    return 3;
  uint64_t length{};
  if (gneiss_scene_instance_serialize(app, scene, nullptr, 0, &length) != GNEISS_SUCCESS)
    return 4;
  std::string json(static_cast<size_t>(length), '\0');
  if (gneiss_scene_instance_serialize(app, scene, json.data(), json.size(), &length) !=
      GNEISS_SUCCESS)
    return 4;
  if (gneiss_scene_instance_unload(app, scene) != GNEISS_SUCCESS)
    return 4;
  scene = 0;
  if (gneiss_application_destroy(app) != GNEISS_SUCCESS)
    return 4;
  app = 0;
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
  if (!open_scene(output_root.generic_string(), app, scene) || !target_for(app, scene, target))
    return 6;
  if (gneiss_type_registry_get_property(registry, gneiss_transform_type_id(), 1, target, &value) !=
          GNEISS_SUCCESS ||
      value.kind != GNEISS_PROPERTY_KIND_VEC3 || std::abs(value.payload.vec3_value.x - 2) > 0.0001F)
    return 6;
  if (gneiss_type_registry_get_property(registry, gneiss_camera_type_id(), 2, target, &value) !=
          GNEISS_SUCCESS ||
      value.kind != GNEISS_PROPERTY_KIND_FLOAT32 ||
      std::abs(value.payload.float32_value - 0.25F) > 0.0001F)
    return 6;
  if (gneiss_scene_instance_unload(app, scene) != GNEISS_SUCCESS)
    return 6;
  scene = 0;
  if (gneiss_application_destroy(app) != GNEISS_SUCCESS)
    return 6;
  app = 0;
  std::filesystem::remove_all(output_root);
  std::puts("属性修改、保存与重新加载成功");
  return 0;
} catch (...) {
  return 99;
}
