// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view scene_uri = "asset://scenes/property.scene.json";
constexpr std::string_view camera_uuid = "37cff772-2e8d-4bc7-9ed2-f94435926d4e";

[[nodiscard]] gneiss::result create_application(std::string_view root,
                                                gneiss::application& output) noexcept {
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  return gneiss::application::create(desc, output);
}

[[nodiscard]] bool inspect_registry(const gneiss::type_registry& registry) {
  std::uint32_t type_count = 0;
  if (registry.type_count(type_count) != gneiss::result::success) {
    return false;
  }
  for (std::uint32_t type_index = 0; type_index < type_count; ++type_index) {
    gneiss_type_info type{};
    if (gneiss_type_registry_type_at(registry.get(), type_index, &type) != GNEISS_SUCCESS) {
      return false;
    }
    std::printf("类型 %.*s\n", static_cast<int>(type.name_length), type.name);
    for (std::uint32_t field_index = 0; field_index < type.field_count; ++field_index) {
      const auto& field = type.fields[field_index];
      std::printf("  字段 %u: %.*s%s\n", field.id, static_cast<int>(field.name_length), field.name,
                  (field.property_capabilities & GNEISS_PROPERTY_CAPABILITY_WRITABLE) != 0U
                      ? "（可写）"
                      : "（只读）");
    }
  }
  return true;
}

[[nodiscard]] bool find_camera(gneiss::application& application,
                               const gneiss::scene_instance& scene, gneiss_world& out_world,
                               gneiss_entity_id& out_entity) {
  gneiss::scene_node_id node;
  return application.get_world(out_world) == gneiss::result::success &&
         scene.find_node(camera_uuid, node) == gneiss::result::success &&
         gneiss_scene_node_get_entity(out_world, node.get(), &out_entity) == GNEISS_SUCCESS;
}

[[nodiscard]] bool set_properties(const gneiss::type_registry& registry, gneiss_world world,
                                  gneiss_entity_id entity) {
  const gneiss_property_target target{.context = world, .object = entity};
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  value.kind = GNEISS_PROPERTY_KIND_VEC3;
  value.payload.vec3_value = {.x = 2.0F, .y = 1.0F, .z = 4.0F};
  if (registry.set_property(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, target,
                            value) != gneiss::result::success) {
    return false;
  }
  value.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  value.payload.float32_value = 0.25F;
  return registry.set_property(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target,
                               value) == gneiss::result::success;
}

[[nodiscard]] bool verify_properties(const gneiss::type_registry& registry, gneiss_world world,
                                     gneiss_entity_id entity) {
  const gneiss_property_target target{.context = world, .object = entity};
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  if (registry.get_property(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, target,
                            value) != gneiss::result::success ||
      value.kind != GNEISS_PROPERTY_KIND_VEC3 ||
      std::abs(value.payload.vec3_value.x - 2.0F) > 0.0001F) {
    return false;
  }
  return registry.get_property(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target,
                               value) == gneiss::result::success &&
         value.kind == GNEISS_PROPERTY_KIND_FLOAT32 &&
         std::abs(value.payload.float32_value - 0.25F) <= 0.0001F;
}

[[nodiscard]] std::filesystem::path make_output_root() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("gneiss-property-inspector-" + std::to_string(suffix));
}

} // namespace

int main() try {
  gneiss::type_registry registry;
  if (gneiss::type_registry::create(registry) != gneiss::result::success ||
      gneiss::world::register_reflection(registry) != gneiss::result::success ||
      registry.freeze() != gneiss::result::success || !inspect_registry(registry)) {
    return 1;
  }

  gneiss::application application;
  gneiss::scene_instance scene;
  constexpr std::string_view source_root = GNEISS_PROPERTY_INSPECTOR_ASSET_ROOT;
  if (create_application(source_root, application) != gneiss::result::success ||
      gneiss::scene_instance::load(application.get(), scene_uri, scene) !=
          gneiss::result::success) {
    return 2;
  }
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  if (!find_camera(application, scene, world, entity) || !set_properties(registry, world, entity)) {
    return 3;
  }

  std::string json;
  if (scene.serialize(json) != gneiss::result::success) {
    return 4;
  }
  scene.reset();
  application.reset();

  const auto output_root = make_output_root();
  const auto scene_path = output_root / "scenes" / "property.scene.json";
  std::filesystem::create_directories(scene_path.parent_path());
  {
    std::ofstream stream(scene_path, std::ios::binary);
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!stream) {
      return 5;
    }
  }

  const auto output_root_text = output_root.generic_string();
  gneiss::application reloaded_application;
  gneiss::scene_instance reloaded_scene;
  if (create_application(output_root_text, reloaded_application) != gneiss::result::success ||
      gneiss::scene_instance::load(reloaded_application.get(), scene_uri, reloaded_scene) !=
          gneiss::result::success ||
      !find_camera(reloaded_application, reloaded_scene, world, entity) ||
      !verify_properties(registry, world, entity)) {
    return 6;
  }
  reloaded_scene.reset();
  reloaded_application.reset();
  std::filesystem::remove_all(output_root);
  std::puts("属性修改、保存与重新加载成功");
  return 0;
} catch (...) {
  return 99;
}
