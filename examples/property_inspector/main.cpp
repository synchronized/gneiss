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
  gneiss::application_desc desc{};
  desc.asset_root = root;
  return gneiss::application::create(desc, output);
}

[[nodiscard]] bool inspect_registry(const gneiss::type_registry& registry) {
  std::uint32_t type_count = 0;
  if (registry.type_count(type_count) != gneiss::result::success) {
    return false;
  }
  for (std::uint32_t type_index = 0; type_index < type_count; ++type_index) {
    gneiss::type_info type{};
    if (registry.type_at(type_index, type).failed()) {
      return false;
    }
    std::printf("类型 %.*s\n", static_cast<int>(type.name.size()), type.name.data());
    for (std::uint32_t field_index = 0; field_index < type.fields.size(); ++field_index) {
      const auto& field = type.fields[field_index];
      std::printf("  字段 %u: %.*s%s\n", field.id.get(), static_cast<int>(field.name.size()),
                  field.name.data(),
                  gneiss::has_flags(field.capabilities, gneiss::property_capabilities::writable)
                      ? "（可写）"
                      : "（只读）");
    }
  }
  return true;
}

[[nodiscard]] bool find_camera(gneiss::application& application,
                               const gneiss::scene_instance& scene, gneiss::world_ref& out_world,
                               gneiss::entity_id& out_entity) {
  gneiss::scene_node_id node;
  return application.get_world(out_world) == gneiss::result::success &&
         scene.find_node(camera_uuid, node) == gneiss::result::success &&
         out_world.get_entity(node, out_entity).ok();
}

[[nodiscard]] bool set_properties(const gneiss::type_registry& registry, gneiss::world_ref world,
                                  gneiss::entity_id entity) {
  const gneiss::property_target target{.context = world.get(), .object = entity.get()};
  gneiss::property_value value{};
  value.payload = gneiss::property_vec3{.x = 2.0F, .y = 1.0F, .z = 4.0F};
  if (registry.set_property(gneiss::transform_type_id(), gneiss::transform_fields::translation,
                            target, value) != gneiss::result::success) {
    return false;
  }
  value.payload = 0.25F;
  return registry.set_property(gneiss::camera_type_id(), gneiss::camera_fields::near_plane, target,
                               value) == gneiss::result::success;
}

[[nodiscard]] bool verify_properties(const gneiss::type_registry& registry, gneiss::world_ref world,
                                     gneiss::entity_id entity) {
  const gneiss::property_target target{.context = world.get(), .object = entity.get()};
  gneiss::property_value value{};
  if (registry.get_property(gneiss::transform_type_id(), gneiss::transform_fields::translation,
                            target, value) != gneiss::result::success ||
      value.kind() != gneiss::property_kind::vec3 ||
      std::abs(std::get<gneiss::property_vec3>(value.payload).x - 2.0F) > 0.0001F) {
    return false;
  }
  return registry.get_property(gneiss::camera_type_id(), gneiss::camera_fields::near_plane, target,
                               value) == gneiss::result::success &&
         value.kind() == gneiss::property_kind::float32 &&
         std::abs(std::get<float>(value.payload) - 0.25F) <= 0.0001F;
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
  gneiss::world_ref world;
  gneiss::entity_id entity;
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
