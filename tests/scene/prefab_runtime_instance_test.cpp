// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/scene/prefab_runtime_instance.hpp"
#include "engine/function/world/world_service.hpp"
#include "engine/resource/cache/resource_cache.hpp"
#include "engine/resource/loaders/prefab_asset_loader.hpp"
#include "engine/resource/vfs/file_system.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"
#include <gneiss/engine/detail/reflection_values.hpp>

#include <gneiss/engine/world.h>

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

class memory_file_system final : public gneiss::engine::resource::file_system {
public:
  [[nodiscard]] gneiss::engine::core::result
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    const auto content =
        path == "lamp.prefab.json"
            ? prefab_json
            : (path == "broken.prefab.json" ? broken_prefab_json : std::string_view{});
    if (content.empty()) {
      return gneiss::engine::core::result::not_found;
    }
    try {
      out_bytes.resize(content.size());
      for (std::size_t index = 0; index < content.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content[index]);
      }
      return gneiss::engine::core::result::success;
    } catch (...) {
      return gneiss::engine::core::result::out_of_memory;
    }
  }
};

bool check_failed_camera_prefab(
    gneiss::engine::world::world& native_world, gneiss_world world, gneiss_type_registry registry,
    gneiss::engine::render::render_asset_loader& render_loader,
    gneiss::engine::resource::prefab_asset_loader& prefab_loader,
    const gneiss::engine::resource::prefab_description& source,
    const std::vector<gneiss::engine::resource::prefab_property_override>& invalid_overrides) {
  auto description = source;
  description.objects[0].camera = {.vertical_field_of_view_radians = 1.0F,
                                   .near_plane = 0.1F,
                                   .far_plane = 100.0F,
                                   .is_primary = true};
  gneiss::engine::resource::prefab_asset_ref lease;
  if (prefab_loader.install_prepared("asset://prefabs/camera.prefab.json", std::move(description),
                                     lease) != gneiss::engine::core::result::success) {
    return false;
  }
  gneiss_entity_id previous{};
  const gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  if (gneiss::engine::core::result{gneiss_world_entity_create(world, &previous)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_entity_configure_camera(
          world, previous, &camera)} != gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_set_active_camera(world, previous)} !=
          gneiss::engine::core::result::success) {
    return false;
  }
  const gneiss::engine::core::math::transform transform = {};
  std::unique_ptr<gneiss::engine::scene::prefab_runtime_instance> candidate;
  const auto operation = gneiss::engine::scene::prefab_runtime_instance::create(
      native_world, world, render_loader, lease, registry, "20000000-0000-4000-8000-000000000005",
      GNEISS_NULL_SCENE_NODE_ID, transform, invalid_overrides, candidate);
  gneiss_entity_id active{};
  std::uint64_t count{};
  const bool preserved =
      operation == gneiss::engine::core::result::not_found && !candidate &&
      gneiss::engine::core::result{gneiss_world_get_active_camera(world, &active)} ==
          gneiss::engine::core::result::success &&
      active == previous &&
      gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} ==
          gneiss::engine::core::result::success &&
      count == 7U;
  if (gneiss::engine::core::result{gneiss_world_entity_destroy(world, previous)} !=
          gneiss::engine::core::result::success ||
      !preserved) {
    return false;
  }
  // 原本没有相机时，失败候选也不能留下活动相机或实体。
  return gneiss::engine::scene::prefab_runtime_instance::create(
             native_world, world, render_loader, lease, registry,
             "20000000-0000-4000-8000-000000000005", GNEISS_NULL_SCENE_NODE_ID, transform,
             invalid_overrides, candidate) == gneiss::engine::core::result::not_found &&
         !candidate &&
         gneiss::engine::core::result{gneiss_world_get_active_camera(world, &active)} ==
             gneiss::engine::core::result::not_ready &&
         gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} ==
             gneiss::engine::core::result::success &&
         count == 6U;
}

bool check_hidden_prefab(gneiss::engine::world::world& native_world, gneiss_world world,
                         gneiss_type_registry registry,
                         gneiss::engine::render::render_asset_loader& loader,
                         gneiss::engine::resource::prefab_asset_loader& prefabs,
                         gneiss::engine::resource::prefab_asset_ref lease,
                         gneiss_scene_node_id parent) {
  using gneiss::engine::scene::prefab_runtime_instance;
  prefab_runtime_instance candidate(native_world, world, loader, lease,
                                    "20000000-0000-4000-8000-000000000006");
  auto prepared = *lease.get();
  prepared.objects[0].translation[0] = 19.0F;
  const gneiss::engine::core::math::transform identity = {};
  std::uint64_t count{};
  if (candidate.begin_hidden(parent, identity, std::move(prepared)) !=
          gneiss::engine::core::result::success ||
      candidate.create_staged_node(0U, candidate.root(), {}, {}) !=
          gneiss::engine::core::result::success ||
      candidate.publish_hidden() != gneiss::engine::core::result::invalid_state) {
    return false;
  }
  prefab_runtime_instance::node_info info;
  gneiss::engine::core::math::transform queried{};
  if (candidate.get_node_info(0U, info) != gneiss::engine::core::result::success ||
      info.parent != candidate.root() ||
      native_world.node_get_local_transform(gneiss::scene_node_id{info.node}, queried) !=
          gneiss::engine::core::result::invalid_handle ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} !=
          gneiss::engine::core::result::success ||
      count != 6U ||
      candidate.create_staged_node(1U, info.node, {}, {}) !=
          gneiss::engine::core::result::success ||
      candidate.publish_hidden() != gneiss::engine::core::result::success ||
      candidate.publish_hidden() != gneiss::engine::core::result::invalid_state ||
      candidate.cancel_staged_step(10U) ||
      native_world.node_get_local_transform(gneiss::scene_node_id{info.node}, queried) !=
          gneiss::engine::core::result::success ||
      queried.translation[0] != 19.0F || lease.get()->objects[0].translation[0] != 1.0F ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} !=
          gneiss::engine::core::result::success ||
      count != 9U) {
    return false;
  }
  auto expanded = *lease.get();
  auto child = expanded.objects[1];
  child.uuid = "10000000-0000-4000-8000-000000000004";
  child.parent_uuid = expanded.objects[1].uuid;
  expanded.objects.push_back(std::move(child));
  gneiss::engine::resource::prefab_asset_ref expanded_lease;
  if (prefabs.install_prepared("asset://prefabs/expanded.prefab.json", std::move(expanded),
                               expanded_lease) != gneiss::engine::core::result::success ||
      candidate.reload(expanded_lease, registry, {}) != gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} !=
          gneiss::engine::core::result::success ||
      count != 10U) {
    return false;
  }
  if (candidate.retire_hidden() != gneiss::engine::core::result::success ||
      native_world.node_get_local_transform(gneiss::scene_node_id{info.node}, queried) !=
          gneiss::engine::core::result::invalid_handle ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} !=
          gneiss::engine::core::result::success ||
      count != 6U || candidate.cancel_staged_step(1U) || candidate.cancel_staged_step(1U) ||
      !candidate.cancel_staged_step(5U) ||
      candidate.publish_hidden() != gneiss::engine::core::result::invalid_state) {
    return false;
  }
  // 提交前取消与析构兜底不得影响现有父节点。
  {
    prefab_runtime_instance aborted(native_world, world, loader, lease,
                                    "20000000-0000-4000-8000-000000000007");
    if (aborted.begin_hidden(parent, identity, *lease.get()) !=
            gneiss::engine::core::result::success ||
        aborted.create_staged_node(0U, aborted.root(), {}, {}) !=
            gneiss::engine::core::result::success ||
        aborted.cancel_staged_step(0U) ||
        aborted.create_staged_node(1U, aborted.root(), {}, {}) !=
            gneiss::engine::core::result::invalid_state) {
      return false;
    }
  }
  return native_world.node_get_local_transform(gneiss::scene_node_id{parent}, queried) ==
             gneiss::engine::core::result::success &&
         gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} ==
             gneiss::engine::core::result::success &&
         count == 6U;
}

} // namespace

int main() try {
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::engine::world::world_owner world_owner;
  if (gneiss::engine::world::create_native(world_owner, world) !=
      gneiss::engine::core::result::success) {
    return 1;
  }
  auto& native_world = *world_owner;
  gneiss_type_registry registry = GNEISS_NULL_TYPE_REGISTRY;
  if (gneiss::engine::core::result{gneiss_type_registry_create(&registry)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_register_reflection(registry)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_type_registry_freeze(registry)} !=
          gneiss::engine::core::result::success) {
    return 2;
  }

  gneiss::engine::resource::virtual_file_system file_system;
  gneiss::engine::render::render_resource_service resources;
  gneiss::engine::resource::resource_cache cache;
  gneiss::engine::render::render_asset_loader render_loader(file_system, cache, resources);
  gneiss::engine::resource::prefab_asset_loader prefab_loader(file_system, cache);
  if (file_system.mount("asset://prefabs/", std::make_shared<memory_file_system>()) !=
      gneiss::engine::core::result::success) {
    return 3;
  }
  gneiss::engine::resource::scene_diagnostic diagnostic;
  gneiss::engine::resource::prefab_asset_ref first_lease;
  gneiss::engine::resource::prefab_asset_ref second_lease;
  if (prefab_loader.acquire("asset://prefabs/lamp.prefab.json", first_lease, diagnostic) !=
          gneiss::engine::core::result::success ||
      prefab_loader.acquire("asset://prefabs/lamp.prefab.json", second_lease, diagnostic) !=
          gneiss::engine::core::result::success) {
    return 4;
  }

  gneiss::engine::core::math::transform first_transform = {};
  first_transform.translation[0] = 10.0F;
  gneiss::engine::core::math::transform second_transform = {};
  second_transform.translation[0] = -5.0F;
  std::unique_ptr<gneiss::engine::scene::prefab_runtime_instance> first;
  std::unique_ptr<gneiss::engine::scene::prefab_runtime_instance> second;
  const std::vector<gneiss::engine::resource::prefab_property_override> no_overrides;
  const std::vector<gneiss::engine::resource::prefab_property_override> first_overrides{{
      .key = {.node = {.instance_uuid = "20000000-0000-4000-8000-000000000001",
                       .source_node_uuid = "10000000-0000-4000-8000-000000000002"},
              .type_id = gneiss::from_native(gneiss_transform_type_id()),
              .field_id = gneiss::field_id{GNEISS_TRANSFORM_FIELD_TRANSLATION}},
      .value = {.payload = std::array{3.0F, 0.0F, 0.0F}},
  }};
  if (gneiss::engine::scene::prefab_runtime_instance::create(
          native_world, world, render_loader, std::move(first_lease), registry,
          "20000000-0000-4000-8000-000000000001", GNEISS_NULL_SCENE_NODE_ID, first_transform,
          first_overrides, first) != gneiss::engine::core::result::success ||
      gneiss::engine::scene::prefab_runtime_instance::create(
          native_world, world, render_loader, std::move(second_lease), registry,
          "20000000-0000-4000-8000-000000000002", GNEISS_NULL_SCENE_NODE_ID, second_transform,
          no_overrides, second) != gneiss::engine::core::result::success ||
      first->node_count() != 2U || second->node_count() != 2U) {
    return 5;
  }
  gneiss::engine::resource::prefab_description projected;
  if (gneiss::engine::scene::prefab_runtime_instance::prepare_candidate_description(
          *first->prefab_lease().get(), registry, "20000000-0000-4000-8000-000000000001",
          first_overrides, projected) != gneiss::engine::core::result::success ||
      projected.objects[0].translation[0] != 3 ||
      first->prefab_lease().get()->objects[0].translation[0] != 1) {
    return 14;
  }
  if (gneiss::engine::scene::prefab_runtime_instance::prepare_candidate_description(
          *first->prefab_lease().get(), registry, "20000000-0000-4000-8000-000000000002",
          first_overrides, projected) != gneiss::engine::core::result::invalid_argument ||
      projected.objects[0].translation[0] != 3) {
    return 15;
  }

  const auto first_source_root = first->find_node("10000000-0000-4000-8000-000000000002");
  const auto second_source_root = second->find_node("10000000-0000-4000-8000-000000000002");
  gneiss::engine::core::math::transform first_world = {};
  gneiss::engine::core::math::transform second_world = {};
  std::uint64_t entity_count = 0;
  if (first_source_root == GNEISS_NULL_SCENE_NODE_ID ||
      second_source_root == GNEISS_NULL_SCENE_NODE_ID || first_source_root == second_source_root ||
      native_world.node_get_world_transform(gneiss::scene_node_id{first_source_root},
                                            first_world) != gneiss::engine::core::result::success ||
      native_world.node_get_world_transform(gneiss::scene_node_id{second_source_root},
                                            second_world) !=
          gneiss::engine::core::result::success ||
      first_world.translation[0] != 13.0F || second_world.translation[0] != -4.0F ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
          gneiss::engine::core::result::success ||
      entity_count != 6U) {
    return 6;
  }

  gneiss::engine::resource::prefab_asset_ref invalid_override_lease;
  if (prefab_loader.acquire("asset://prefabs/lamp.prefab.json", invalid_override_lease,
                            diagnostic) != gneiss::engine::core::result::success) {
    return 7;
  }
  const std::vector<gneiss::engine::resource::prefab_property_override> invalid_overrides{{
      .key = {.node = {.instance_uuid = "20000000-0000-4000-8000-000000000005",
                       .source_node_uuid = "10000000-0000-4000-8000-000000000099"},
              .type_id = gneiss::from_native(gneiss_transform_type_id()),
              .field_id = gneiss::field_id{GNEISS_TRANSFORM_FIELD_TRANSLATION}},
      .value = {.payload = std::array{3.0F, 0.0F, 0.0F}},
  }};
  std::unique_ptr<gneiss::engine::scene::prefab_runtime_instance> failed;
  if (!check_hidden_prefab(native_world, world, registry, render_loader, prefab_loader,
                           first->prefab_lease(), first->root())) {
    return 17;
  }
  if (!check_failed_camera_prefab(native_world, world, registry, render_loader, prefab_loader,
                                  *first->prefab_lease().get(), invalid_overrides)) {
    return 16;
  }
  if (gneiss::engine::scene::prefab_runtime_instance::create(
          native_world, world, render_loader, std::move(invalid_override_lease), registry,
          "20000000-0000-4000-8000-000000000005", GNEISS_NULL_SCENE_NODE_ID, first_transform,
          invalid_overrides, failed) != gneiss::engine::core::result::not_found ||
      failed ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
          gneiss::engine::core::result::success ||
      entity_count != 6U) {
    return 7;
  }

  gneiss::engine::resource::prefab_asset_ref failure_lease;
  if (prefab_loader.acquire("asset://prefabs/lamp.prefab.json", failure_lease, diagnostic) !=
      gneiss::engine::core::result::success) {
    return 7;
  }
  gneiss::engine::core::math::transform invalid_transform = {};
  invalid_transform.scale[1] = 0.0F;
  if (gneiss::engine::scene::prefab_runtime_instance::create(
          native_world, world, render_loader, std::move(failure_lease), registry,
          "20000000-0000-4000-8000-000000000003", GNEISS_NULL_SCENE_NODE_ID, invalid_transform,
          no_overrides, failed) != gneiss::engine::core::result::invalid_argument ||
      failed ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
          gneiss::engine::core::result::success ||
      entity_count != 6U) {
    return 8;
  }

  gneiss::engine::resource::prefab_asset_ref broken_lease;
  if (prefab_loader.acquire("asset://prefabs/broken.prefab.json", broken_lease, diagnostic) !=
      gneiss::engine::core::result::success) {
    return 9;
  }
  if (gneiss::engine::scene::prefab_runtime_instance::create(
          native_world, world, render_loader, std::move(broken_lease), registry,
          "20000000-0000-4000-8000-000000000004", GNEISS_NULL_SCENE_NODE_ID, first_transform,
          no_overrides, failed) != gneiss::engine::core::result::not_found ||
      failed ||
      gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
          gneiss::engine::core::result::success ||
      entity_count != 6U) {
    return 10;
  }

  {
    gneiss::engine::scene::prefab_runtime_instance staged(native_world, world, render_loader,
                                                          second->prefab_lease(),
                                                          "20000000-0000-4000-8000-000000000005");
    if (staged.begin_staged(GNEISS_NULL_SCENE_NODE_ID, first_transform) !=
            gneiss::engine::core::result::success ||
        staged.create_staged_node(0U, staged.root(), {}, {}) !=
            gneiss::engine::core::result::success ||
        staged.create_staged_node(1U, staged.find_node("10000000-0000-4000-8000-000000000002"), {},
                                  {}) != gneiss::engine::core::result::success ||
        staged.cancel_staged_step(0U) ||
        gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
            gneiss::engine::core::result::success ||
        entity_count != 9U || staged.cancel_staged_step(1U) ||
        gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
            gneiss::engine::core::result::success ||
        entity_count != 8U ||
        staged.create_staged_node(1U, staged.root(), {}, {}) !=
            gneiss::engine::core::result::invalid_state ||
        staged.cancel_staged_step(1U) ||
        gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
            gneiss::engine::core::result::success ||
        entity_count != 7U || staged.cancel_staged_step(2U) || !staged.cancel_staged_step(1U) ||
        !staged.cancel_staged_step(1U) ||
        gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
            gneiss::engine::core::result::success ||
        entity_count != 6U) {
      return 13;
    }
  }
  first.reset();
  if (gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
          gneiss::engine::core::result::success ||
      entity_count != 3U ||
      native_world.node_get_world_transform(gneiss::scene_node_id{first_source_root},
                                            first_world) !=
          gneiss::engine::core::result::invalid_handle ||
      native_world.node_get_world_transform(gneiss::scene_node_id{second_source_root},
                                            second_world) !=
          gneiss::engine::core::result::success) {
    return 11;
  }
  second.reset();
  if (gneiss::engine::core::result{gneiss_world_entity_count(world, &entity_count)} !=
          gneiss::engine::core::result::success ||
      entity_count != 0U ||
      gneiss::engine::core::result{gneiss_type_registry_destroy(registry)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_destroy(world)} !=
          gneiss::engine::core::result::invalid_state) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
