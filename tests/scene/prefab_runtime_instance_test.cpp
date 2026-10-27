// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/asset_manager.hpp"
#include "engine/resource/resource_cache.hpp"
#include "engine/resource/scene/prefab_description.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view prefab_json = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000001",
  "objects":[
    {
      "uuid":"10000000-0000-4000-8000-000000000002",
      "parent":null,
      "transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{}
    },
    {
      "uuid":"10000000-0000-4000-8000-000000000003",
      "parent":"10000000-0000-4000-8000-000000000002",
      "transform":{"translation":[0,2,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":{}
    }
  ]
})";

constexpr std::string_view broken_prefab_json = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000011",
  "objects":[{
    "uuid":"10000000-0000-4000-8000-000000000012",
    "parent":null,
    "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
    "components":{"mesh_renderer":{"mesh":"asset://missing.mesh","material":"asset://missing.material"}}
  }]
})";

class memory_file_system final : public gneiss::test_support::memory_file_fixture {
public:
  gneiss::engine::result read_bounded(std::string_view path, std::size_t limit,
                                      std::vector<std::byte>& output) const noexcept override {
    const auto content =
        path == "lamp.prefab.json"
            ? prefab_json
            : (path == "broken.prefab.json" ? broken_prefab_json : std::string_view{});
    if (content.size() > limit)
      return gneiss::engine::result::invalid_argument;
    return read(path, output);
  }
  [[nodiscard]] gneiss::engine::result
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    const auto content =
        path == "lamp.prefab.json"
            ? prefab_json
            : (path == "broken.prefab.json" ? broken_prefab_json : std::string_view{});
    if (content.empty()) {
      return gneiss::engine::result::not_found;
    }
    try {
      out_bytes.resize(content.size());
      for (std::size_t index = 0; index < content.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content[index]);
      }
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::out_of_memory;
    }
  }
};

bool check_failed_camera_prefab(
    gneiss::engine::world& native_world, const gneiss::engine::meta::registry_ref& registry,
    gneiss::engine::render_asset_loader& render_loader,
    gneiss::engine::asset_manager& prefab_loader, const gneiss::engine::prefab_description& source,
    const std::vector<gneiss::engine::prefab_property_override>& invalid_overrides) {
  auto description = source;
  description.objects[0].camera = {.vertical_field_of_view_radians = 1.0F,
                                   .near_plane = 0.1F,
                                   .far_plane = 100.0F,
                                   .is_primary = true};
  gneiss::engine::prefab_asset_ref lease;
  if (prefab_loader.store<gneiss::engine::prefab_description>(
          "asset://prefabs/camera.prefab.json",
          std::make_shared<const gneiss::engine::prefab_description>(std::move(description)),
          lease) != gneiss::engine::result::success) {
    return false;
  }
  std::uint64_t previous{};
  const gneiss::engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (gneiss::engine::result{gneiss::engine::write_identifier<gneiss::entity_id>(
          previous, [&](auto& output) { return native_world.entity_create(output); })} !=
          gneiss::engine::result::success ||
      gneiss::engine::result{native_world.entity_configure_camera(
          gneiss::entity_id{previous}, camera)} != gneiss::engine::result::success ||
      gneiss::engine::result{native_world.set_active_camera(gneiss::entity_id{previous})} !=
          gneiss::engine::result::success) {
    return false;
  }
  const gneiss::engine::math::transform transform = {};
  std::unique_ptr<gneiss::engine::prefab_runtime_instance> candidate;
  const auto operation = gneiss::engine::prefab_runtime_instance::create(
      native_world, render_loader, lease, registry, "20000000-0000-4000-8000-000000000005",
      gneiss::null_scene_node_id, transform, invalid_overrides, candidate);
  std::uint64_t active{};
  std::uint64_t count{};
  const bool preserved =
      operation == gneiss::engine::result::not_found && !candidate &&
      gneiss::engine::result{gneiss::engine::write_identifier<gneiss::entity_id>(
          active, [&](auto& output) { return native_world.get_active_camera(output); })} ==
          gneiss::engine::result::success &&
      active == previous &&
      gneiss::engine::result{native_world.entity_count(count)} == gneiss::engine::result::success &&
      count == 7U;
  if (gneiss::engine::result{native_world.entity_destroy(gneiss::entity_id{previous})} !=
          gneiss::engine::result::success ||
      !preserved) {
    return false;
  }
  // 原本没有相机时，失败候选也不能留下活动相机或实体。
  return gneiss::engine::prefab_runtime_instance::create(
             native_world, render_loader, lease, registry, "20000000-0000-4000-8000-000000000005",
             gneiss::null_scene_node_id, transform, invalid_overrides,
             candidate) == gneiss::engine::result::not_found &&
         !candidate &&
         gneiss::engine::result{gneiss::engine::write_identifier<gneiss::entity_id>(
             active, [&](auto& output) { return native_world.get_active_camera(output); })} ==
             gneiss::engine::result::not_ready &&
         gneiss::engine::result{native_world.entity_count(count)} ==
             gneiss::engine::result::success &&
         count == 6U;
}

bool check_visible_prefab(gneiss::engine::world& world, gneiss::engine::render_asset_loader& loader,
                          gneiss::engine::prefab_asset_ref source, gneiss::scene_node_id parent) {
  std::uint64_t before{}, during{}, after{};
  if (world.entity_count(before).failed())
    return false;
  gneiss::scene_node_id created;
  {
    gneiss::engine::prefab_runtime_instance instance(world, loader, source,
                                                     "20000000-0000-4000-8000-000000000006");
    if (instance.create_root(parent, {}).failed() ||
        instance.create_node(0, instance.root(), {}, {}).failed())
      return false;
    created = instance.root();
    gneiss::engine::math::transform transform;
    if (world.node_get_local_transform(created, transform).failed() ||
        world.entity_count(during).failed() || during != before + 2U)
      return false;
    // 部分创建直接可见；放弃时由实例拥有者回收，不依赖隐藏组。
  }
  gneiss::engine::math::transform transform;
  return world.entity_count(after).ok() && after == before &&
         world.node_get_local_transform(created, transform) ==
             gneiss::engine::result::invalid_handle &&
         world.node_get_local_transform(parent, transform).ok();
}

} // namespace

int main() try {

  gneiss::engine::world_owner world_owner;
  if (gneiss::engine::create_native(world_owner) != gneiss::engine::result::success) {
    return 1;
  }
  auto& native_world = *world_owner;
  gneiss::engine::meta::registry_ref registry;
  if (gneiss::engine::create_property_registry(registry).failed())
    return 2;

  gneiss::test_support::fixture_files file_system;
  gneiss::engine::render_resource_service resources;
  if (file_system.mount("asset://prefabs/", std::make_shared<memory_file_system>()) !=
      gneiss::engine::result::success) {
    return 3;
  }
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(file_system));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  gneiss::engine::render_asset_loader render_loader(asset_owner, resources);
  auto& prefab_loader = asset_owner;
  gneiss::engine::prefab_asset_ref first_lease;
  gneiss::engine::prefab_asset_ref second_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       first_lease) != gneiss::engine::result::success ||
      gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       second_lease) != gneiss::engine::result::success) {
    return 4;
  }

  gneiss::engine::math::transform first_transform = {};
  first_transform.translation[0] = 10.0F;
  gneiss::engine::math::transform second_transform = {};
  second_transform.translation[0] = -5.0F;
  std::unique_ptr<gneiss::engine::prefab_runtime_instance> first;
  std::unique_ptr<gneiss::engine::prefab_runtime_instance> second;
  const std::vector<gneiss::engine::prefab_property_override> no_overrides;
  const std::vector<gneiss::engine::prefab_property_override> first_overrides{{
      .key = {.node = {.instance_uuid = "20000000-0000-4000-8000-000000000001",
                       .source_node_uuid = "10000000-0000-4000-8000-000000000002"},
              .type_id = gneiss::engine::transform_component_type,
              .field_id = gneiss::engine::transform_translation_field},
      .value = {.payload = gneiss::engine::math::vector3{3.0F, 0.0F, 0.0F}},
  }};
  if (gneiss::engine::prefab_runtime_instance::create(
          native_world, render_loader, std::move(first_lease), registry,
          "20000000-0000-4000-8000-000000000001", gneiss::null_scene_node_id, first_transform,
          first_overrides, first) != gneiss::engine::result::success ||
      gneiss::engine::prefab_runtime_instance::create(
          native_world, render_loader, std::move(second_lease), registry,
          "20000000-0000-4000-8000-000000000002", gneiss::null_scene_node_id, second_transform,
          no_overrides, second) != gneiss::engine::result::success ||
      first->node_count() != 2U || second->node_count() != 2U) {
    return 5;
  }
  gneiss::engine::prefab_description projected;
  if (gneiss::engine::prefab_runtime_instance::prepare_description(
          *first->prefab_lease().get(), registry, "20000000-0000-4000-8000-000000000001",
          first_overrides, projected) != gneiss::engine::result::success ||
      projected.objects[0].translation[0] != 3 ||
      first->prefab_lease().get()->objects[0].translation[0] != 1) {
    return 14;
  }
  if (gneiss::engine::prefab_runtime_instance::prepare_description(
          *first->prefab_lease().get(), registry, "20000000-0000-4000-8000-000000000002",
          first_overrides, projected) != gneiss::engine::result::invalid_argument ||
      projected.objects[0].translation[0] != 3) {
    return 15;
  }

  const auto first_source_root = first->find_node("10000000-0000-4000-8000-000000000002");
  const auto second_source_root = second->find_node("10000000-0000-4000-8000-000000000002");
  gneiss::engine::math::transform first_world = {};
  gneiss::engine::math::transform second_world = {};
  std::uint64_t entity_count = 0;
  if (first_source_root == gneiss::null_scene_node_id ||
      second_source_root == gneiss::null_scene_node_id || first_source_root == second_source_root ||
      native_world.node_get_world_transform(gneiss::scene_node_id{first_source_root},
                                            first_world) != gneiss::engine::result::success ||
      native_world.node_get_world_transform(gneiss::scene_node_id{second_source_root},
                                            second_world) != gneiss::engine::result::success ||
      first_world.translation[0] != 13.0F || second_world.translation[0] != -4.0F ||
      gneiss::engine::result{native_world.entity_count(entity_count)} !=
          gneiss::engine::result::success ||
      entity_count != 6U) {
    return 6;
  }

  gneiss::engine::prefab_asset_ref invalid_override_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       invalid_override_lease) != gneiss::engine::result::success) {
    return 7;
  }
  const std::vector<gneiss::engine::prefab_property_override> invalid_overrides{{
      .key = {.node = {.instance_uuid = "20000000-0000-4000-8000-000000000005",
                       .source_node_uuid = "10000000-0000-4000-8000-000000000099"},
              .type_id = gneiss::engine::transform_component_type,
              .field_id = gneiss::engine::transform_translation_field},
      .value = {.payload = gneiss::engine::math::vector3{3.0F, 0.0F, 0.0F}},
  }};
  std::unique_ptr<gneiss::engine::prefab_runtime_instance> failed;
  if (!check_visible_prefab(native_world, render_loader, first->prefab_lease(), first->root())) {
    return 17;
  }
  if (!check_failed_camera_prefab(native_world, registry, render_loader, prefab_loader,
                                  *first->prefab_lease().get(), invalid_overrides)) {
    return 16;
  }
  if (gneiss::engine::prefab_runtime_instance::create(
          native_world, render_loader, std::move(invalid_override_lease), registry,
          "20000000-0000-4000-8000-000000000005", gneiss::null_scene_node_id, first_transform,
          invalid_overrides, failed) != gneiss::engine::result::not_found ||
      failed ||
      gneiss::engine::result{native_world.entity_count(entity_count)} !=
          gneiss::engine::result::success ||
      entity_count != 6U) {
    return 7;
  }

  gneiss::engine::prefab_asset_ref failure_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       failure_lease) != gneiss::engine::result::success) {
    return 7;
  }
  gneiss::engine::math::transform invalid_transform = {};
  invalid_transform.scale[1] = 0.0F;
  if (gneiss::engine::prefab_runtime_instance::create(
          native_world, render_loader, std::move(failure_lease), registry,
          "20000000-0000-4000-8000-000000000003", gneiss::null_scene_node_id, invalid_transform,
          no_overrides, failed) != gneiss::engine::result::invalid_argument ||
      failed ||
      gneiss::engine::result{native_world.entity_count(entity_count)} !=
          gneiss::engine::result::success ||
      entity_count != 6U) {
    return 8;
  }

  gneiss::engine::prefab_asset_ref broken_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/broken.prefab.json",
                                       broken_lease) != gneiss::engine::result::success) {
    return 9;
  }
  if (gneiss::engine::prefab_runtime_instance::create(
          native_world, render_loader, std::move(broken_lease), registry,
          "20000000-0000-4000-8000-000000000004", gneiss::null_scene_node_id, first_transform,
          no_overrides, failed) != gneiss::engine::result::not_found ||
      failed ||
      gneiss::engine::result{native_world.entity_count(entity_count)} !=
          gneiss::engine::result::success ||
      entity_count != 6U) {
    return 10;
  }

  first.reset();
  if (gneiss::engine::result{native_world.entity_count(entity_count)} !=
          gneiss::engine::result::success ||
      entity_count != 3U ||
      native_world.node_get_world_transform(gneiss::scene_node_id{first_source_root},
                                            first_world) !=
          gneiss::engine::result::invalid_handle ||
      native_world.node_get_world_transform(gneiss::scene_node_id{second_source_root},
                                            second_world) != gneiss::engine::result::success) {
    return 11;
  }
  second.reset();
  if (gneiss::engine::result{native_world.entity_count(entity_count)} !=
          gneiss::engine::result::success ||
      entity_count != 0U) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
