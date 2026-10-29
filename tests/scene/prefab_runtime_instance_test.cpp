// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_asset.hpp"
#include "../support/memory_file_fixture.hpp"
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/resource_cache.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/function/scene/prefab_binding.hpp"
#include "engine/function/scene/structural_diff.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/resource/scene/prefab_description.hpp"
#include <stdexcept>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gneiss;
using namespace gneiss::engine;
// 必须先于候选实例构造，保证候选释放后再恢复原相机；异常路径同样适用。
struct camera_restore_guard {
  gneiss::engine::world& native_world;
  std::uint64_t previous;
  bool committed{};
  ~camera_restore_guard() {
    if (!committed && previous != UINT64_C(0)) {
      (void)native_world.set_active_camera(gneiss::entity_id{previous});
    }
  }
};

gneiss::engine::error
create_prefab_binding(world& native_world, resource_service& loader, prefab_asset_ref prefab,
                      gneiss::engine::guid instance_uuid, scene_node_id parent,
                      const math::transform& root_transform,
                      const std::vector<prefab_property_override>& overrides,
                      std::unique_ptr<prefab_binding>& out_instance) noexcept {
  out_instance.reset();
  if (!prefab || prefab.get()->objects.empty()) {
    return engine::error::invalid_argument;
  }
  try {
    if (!engine::is_valid_prefab_author_address({instance_uuid, prefab.get()->objects[0].uuid})) {
      return engine::error::invalid_argument;
    }
    std::uint64_t previous_camera = UINT64_C(0);
    const auto camera_query =
        engine::write_identifier<entity_id>(previous_camera, [&](auto& native_output) {
          return native_world.get_active_camera(native_output);
        });
    if (camera_query != engine::error::success && camera_query != engine::error::not_ready) {
      return camera_query;
    }
    camera_restore_guard camera_guard{.native_world = native_world, .previous = previous_camera};
    auto instance =
        std::make_unique<prefab_binding>(native_world, loader, std::move(prefab), instance_uuid);
    const auto& source = *instance->prefab_lease().get();
    structural_diff order;
    auto result = build_structural_diff({}, source.objects, order);
    if (result.failed())
      return result;
    result = instance->create_root(parent, root_transform);
    if (result.failed())
      return result;
    scene_subtree_guard guard(native_world, instance->root());
    for (const auto& addition : order.added) {
      const auto& object = source.objects[addition.new_index];
      mesh_renderer renderer;
      if (object.components.template get<gneiss::engine::mesh_renderer_description>()) {
        auto mesh = loader.find<mesh_resource>(
            object.components.template get<gneiss::engine::mesh_renderer_description>()->mesh_uri);
        if (!mesh)
          return mesh.error();
        auto material = loader.find<material_resource>(
            object.components.template get<gneiss::engine::mesh_renderer_description>()
                ->material_uri);
        if (!material)
          return material.error();
        renderer = {*mesh, *material};
      }
      const auto node_parent =
          object.parent_uuid ? instance->find_node(*object.parent_uuid) : instance->root();
      result = instance->create_node(addition.new_index, object, node_parent, renderer.mesh,
                                     renderer.material);
      if (result.failed())
        return result;
    }
    if (result == engine::error::success) {
      result = error::success;
      for (const auto& value : overrides) {
        result = instance->apply_override(value);
        if (result.failed())
          break;
      }
    }
    if (result != engine::error::success) {
      return result;
    }
    out_instance = std::move(instance);
    guard.release();
    camera_guard.committed = true;
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

constexpr std::string_view prefab_json = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000001","root_uuid":"10000000-0000-4000-8000-000000000002",
  "objects":[
    {
      "uuid":"10000000-0000-4000-8000-000000000002",
      "parent":null,
      "transform":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":[]
    },
    {
      "uuid":"10000000-0000-4000-8000-000000000003",
      "parent":"10000000-0000-4000-8000-000000000002",
      "transform":{"translation":[0,2,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
      "components":[]
    }
  ]
})";

constexpr std::string_view broken_prefab_json = R"({
  "format":"gneiss.prefab",
  "version":1,
  "prefab_uuid":"10000000-0000-4000-8000-000000000011","root_uuid":"10000000-0000-4000-8000-000000000012",
  "objects":[{
    "uuid":"10000000-0000-4000-8000-000000000012",
    "parent":null,
    "transform":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
    "components":[{"$type_guid":"11a0217a-2eab-4f61-8011-4a63a26e0102","$content":{"mesh":"asset://missing.mesh","material":"asset://missing.material"}}]
  }]
})";

class memory_file_system final : public gneiss::test_support::memory_file_fixture {
public:
  gneiss::engine::error read_bounded(std::string_view path, std::size_t limit,
                                     std::vector<std::byte>& output) const noexcept override {
    const auto content =
        path == "lamp.prefab.json"
            ? prefab_json
            : (path == "broken.prefab.json" ? broken_prefab_json : std::string_view{});
    if (content.size() > limit)
      return gneiss::engine::error::invalid_argument;
    return read(path, output);
  }
  [[nodiscard]] gneiss::engine::error
  read(std::string_view path, std::vector<std::byte>& out_bytes) const noexcept override {
    const auto content =
        path == "lamp.prefab.json"
            ? prefab_json
            : (path == "broken.prefab.json" ? broken_prefab_json : std::string_view{});
    if (content.empty()) {
      return gneiss::engine::error::not_found;
    }
    try {
      out_bytes.resize(content.size());
      for (std::size_t index = 0; index < content.size(); ++index) {
        out_bytes[index] = static_cast<std::byte>(content[index]);
      }
      return gneiss::engine::error::success;
    } catch (...) {
      return gneiss::engine::error::out_of_memory;
    }
  }
};

bool check_explicit_root(gneiss::engine::world& world, gneiss::engine::resource_service& loader,
                         const gneiss::engine::prefab_description& original) {
  namespace e = gneiss::engine;
  auto data = original;
  for (auto& object : data.objects)
    object.components = {};
  std::reverse(data.objects.begin(), data.objects.end());
  e::prefab_asset_ref source{std::make_shared<const e::prefab_description>(data)};
  std::unique_ptr<e::prefab_binding> instance;
  if (create_prefab_binding(world, loader, source,
                            *e::guid::parse("20000000-0000-4000-8000-000000000099"), {}, {}, {},
                            instance)
          .failed())
    return false;
  e::scene_subtree_guard cleanup(world, instance->root());
  const auto root = instance->root();
  if (instance->find_node(data.root_uuid) != root)
    return false;
  std::reverse(data.objects.begin(), data.objects.end());
  e::prefab_asset_ref reordered{std::make_shared<const e::prefab_description>(data)};
  if (instance->reload(reordered, {}).failed() || instance->root() != root ||
      instance->find_node(data.root_uuid) != root)
    return false;
  const auto child = data.objects.back().uuid;
  data.objects.front().parent_uuid = child;
  data.objects.back().parent_uuid.reset();
  data.root_uuid = child;
  e::prefab_asset_ref changed{std::make_shared<const e::prefab_description>(data)};
  return instance->validate_reload(changed, {}) == e::error::unsupported &&
         instance->root() == root && instance->find_node(original.root_uuid) == root;
}

bool check_failed_camera_prefab(
    gneiss::engine::world& native_world, gneiss::engine::resource_service& render_loader,
    gneiss::engine::asset_manager& prefab_loader, const gneiss::engine::prefab_description& source,
    const std::vector<gneiss::engine::prefab_property_override>& invalid_overrides) {
  auto description = source;
  description.objects[0].components.set(
      gneiss::engine::camera_description{.settings = {1.0F, 0.1F, 100.0F}, .is_primary = true});
  gneiss::engine::prefab_asset_ref lease;
  if (gneiss::test_support::assign_value(
          prefab_loader.store<gneiss::engine::prefab_description>(
              "asset://prefabs/camera.prefab.json",
              std::make_shared<const gneiss::engine::prefab_description>(std::move(description))),
          lease) != gneiss::engine::error::success) {
    return false;
  }
  std::uint64_t previous{};
  const gneiss::engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (gneiss::engine::error{gneiss::engine::write_identifier<gneiss::entity_id>(
          previous, [&](auto& output) { return native_world.entity_create(output); })} !=
          gneiss::engine::error::success ||
      gneiss::engine::error{native_world.entity_configure_camera(
          gneiss::entity_id{previous}, camera)} != gneiss::engine::error::success ||
      gneiss::engine::error{native_world.set_active_camera(gneiss::entity_id{previous})} !=
          gneiss::engine::error::success) {
    return false;
  }
  const gneiss::engine::math::transform transform = {};
  std::unique_ptr<gneiss::engine::prefab_binding> candidate;
  const auto operation =
      create_prefab_binding(native_world, render_loader, lease,
                            *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000005"),
                            gneiss::null_scene_node_id, transform, invalid_overrides, candidate);
  std::uint64_t active{};
  std::uint64_t count{};
  const bool preserved =
      operation == gneiss::engine::error::not_found && !candidate &&
      gneiss::engine::error{gneiss::engine::write_identifier<gneiss::entity_id>(
          active, [&](auto& output) { return native_world.get_active_camera(output); })} ==
          gneiss::engine::error::success &&
      active == previous &&
      gneiss::engine::error{gneiss::test_support::assign_value(
          native_world.entity_count(), count)} == gneiss::engine::error::success &&
      count == 5U;
  if (gneiss::engine::error{native_world.entity_destroy(gneiss::entity_id{previous})} !=
          gneiss::engine::error::success ||
      !preserved) {
    return false;
  }
  // 原本没有相机时，失败候选也不能留下活动相机或实体。
  return create_prefab_binding(native_world, render_loader, lease,
                               *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000005"),
                               gneiss::null_scene_node_id, transform, invalid_overrides,
                               candidate) == gneiss::engine::error::not_found &&
         !candidate &&
         gneiss::engine::error{gneiss::engine::write_identifier<gneiss::entity_id>(
             active, [&](auto& output) { return native_world.get_active_camera(output); })} ==
             gneiss::engine::error::not_ready &&
         gneiss::engine::error{gneiss::test_support::assign_value(
             native_world.entity_count(), count)} == gneiss::engine::error::success &&
         count == 4U;
}

bool check_visible_prefab(gneiss::engine::world& world, gneiss::engine::resource_service& loader,
                          gneiss::engine::prefab_asset_ref source, gneiss::scene_node_id parent) {
  std::uint64_t before{}, during{}, after{};
  if (gneiss::test_support::assign_value(world.entity_count(), before).failed())
    return false;
  gneiss::scene_node_id created;
  {
    gneiss::engine::prefab_binding instance(
        world, loader, source,
        *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000006"));
    if (instance.create_root(parent, {}).failed() ||
        instance.create_node(0, source->objects[0], instance.root(), {}, {}).failed())
      return false;
    created = instance.root();
    gneiss::engine::math::transform transform;
    if (world.node_get_local_transform(created, transform).failed() ||
        gneiss::test_support::assign_value(world.entity_count(), during).failed() ||
        during != before + 1U)
      return false;
    // 关系对象不拥有节点；测试调用方显式负责删除。
    if (world.object_destroy(created).failed())
      return false;
  }
  gneiss::engine::math::transform transform;
  return gneiss::test_support::assign_value(world.entity_count(), after).ok() && after == before &&
         world.node_get_local_transform(created, transform) ==
             gneiss::engine::error::invalid_handle &&
         world.node_get_local_transform(parent, transform).ok();
}

} // namespace

int main() try {
  if (gneiss::engine::initialize_meta().failed())
    return 99;

  gneiss::engine::world_owner world_owner;
  if (gneiss::test_support::assign_value(gneiss::engine::create_native(), world_owner) !=
      gneiss::engine::error::success) {
    return 1;
  }
  auto& native_world = *world_owner;
  if (gneiss::engine::initialize_meta().failed())
    return 2;

  gneiss::test_support::fixture_files file_system;
  gneiss::engine::render_service resources;

  if (file_system.mount("asset://prefabs/", std::make_shared<memory_file_system>()) !=
      gneiss::engine::error::success) {
    return 3;
  }
  gneiss::engine::asset_manager asset_owner(gneiss::test_support::test_files(file_system));
  [[maybe_unused]] auto& cache = asset_owner.cache();
  gneiss::engine::resource_service render_loader{asset_owner};
  if (render_loader.attach_render(resources).failed())
    throw std::runtime_error("资源管理器渲染绑定失败");
  auto& prefab_loader = asset_owner;
  gneiss::engine::prefab_asset_ref first_lease;
  gneiss::engine::prefab_asset_ref second_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       first_lease) != gneiss::engine::error::success ||
      gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       second_lease) != gneiss::engine::error::success) {
    return 4;
  }

  if (!check_explicit_root(native_world, render_loader, *first_lease))
    return 96;

  gneiss::engine::math::transform first_transform = {};
  first_transform.translation[0] = 10.0F;
  first_transform.scale = {2.0F, 3.0F, 4.0F};
  first_transform.rotation = gneiss::engine::math::quaternion{0.70710678F, 0.0F, 0.70710678F, 0.0F};
  gneiss::engine::math::transform second_transform = {};
  second_transform.translation[0] = -5.0F;
  std::unique_ptr<gneiss::engine::prefab_binding> first;
  std::unique_ptr<gneiss::engine::prefab_binding> second;
  const std::vector<gneiss::engine::prefab_property_override> no_overrides;
  const std::vector<gneiss::engine::prefab_property_override> first_overrides{{
      .key = {.source_node_uuid =
                  *gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000003"),
              .type_guid = gneiss::engine::transform_component_type,
              .property_id = gneiss::engine::transform_translation_field},
      .value = {.payload = gneiss::engine::math::vector3{3.0F, 0.0F, 0.0F}},
  }};
  if (create_prefab_binding(native_world, render_loader, std::move(first_lease),
                            *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000001"),
                            gneiss::null_scene_node_id, first_transform, first_overrides,
                            first) != gneiss::engine::error::success ||
      create_prefab_binding(native_world, render_loader, std::move(second_lease),
                            *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000002"),
                            gneiss::null_scene_node_id, second_transform, no_overrides,
                            second) != gneiss::engine::error::success ||
      first->node_count() != 2U || second->node_count() != 2U) {
    return 5;
  }
  gneiss::engine::prefab_description projected;
  if (gneiss::engine::prepare_prefab_description(*first->prefab_lease().get(), first_overrides,
                                                 projected) != gneiss::engine::error::success ||
      projected.objects[1].transform.translation[0] != 3 ||
      first->prefab_lease().get()->objects[0].transform.translation[0] != 1) {
    return 14;
  }
  // 相同的相对覆盖可以用于另一实例，不携带实例身份。
  if (second->reload(second->prefab_lease(), first_overrides).failed())
    return 15;

  const auto first_source_root =
      first->find_node(*gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000002"));
  const auto second_source_root =
      second->find_node(*gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000002"));
  gneiss::engine::math::transform first_world = {};
  gneiss::engine::math::transform second_world = {};
  std::uint64_t entity_count = 0;
  if (first_source_root != first->root() || second_source_root != second->root() ||
      first_source_root == gneiss::null_scene_node_id ||
      second_source_root == gneiss::null_scene_node_id || first_source_root == second_source_root ||
      native_world.node_get_world_transform(gneiss::scene_node_id{first_source_root},
                                            first_world) != gneiss::engine::error::success ||
      native_world.node_get_world_transform(gneiss::scene_node_id{second_source_root},
                                            second_world) != gneiss::engine::error::success ||
      first_world.translation[0] != 10.0F || second_world.translation[0] != -5.0F ||
      gneiss::engine::error{gneiss::test_support::assign_value(
          native_world.entity_count(), entity_count)} != gneiss::engine::error::success ||
      entity_count != 4U) {
    return 6;
  }

  // 实例根直接映射来源根；外层 TRS 不与来源根相乘，来源重载不得覆盖实例值。
  gneiss::engine::math::transform actual_root;
  if (native_world.node_get_local_transform(first->root(), actual_root).failed() ||
      actual_root.translation != first_transform.translation ||
      actual_root.rotation != first_transform.rotation ||
      actual_root.scale != first_transform.scale)
    return 23;
  auto root_override = first_overrides.front();
  root_override.key.source_node_uuid =
      *gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000002");
  if (first->apply_override(root_override) != gneiss::engine::error::invalid_argument)
    return 24;
  auto revision = std::make_shared<gneiss::engine::prefab_description>(*first->prefab_lease());
  revision->objects[0].name = "New source name";
  revision->objects[0].transform.translation = {50.0F, 60.0F, 70.0F};
  revision->objects[0].transform.scale = {7.0F, 8.0F, 9.0F};
  if (native_world
          .node_set_identity(first->root(),
                             {*gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000001"),
                              "Instance name"})
          .failed() ||
      first->reload(gneiss::engine::prefab_asset_ref{revision}, first_overrides).failed() ||
      native_world.node_get_local_transform(first->root(), actual_root).failed() ||
      actual_root.translation != first_transform.translation ||
      actual_root.rotation != first_transform.rotation ||
      actual_root.scale != first_transform.scale ||
      gneiss::engine::scene_object_identity(native_world, first->root()).name != "Instance name")
    return 25;
  gneiss::engine::prefab_asset_ref invalid_override_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       invalid_override_lease) != gneiss::engine::error::success) {
    return 7;
  }
  const std::vector<gneiss::engine::prefab_property_override> invalid_overrides{{
      .key = {.source_node_uuid =
                  *gneiss::engine::guid::parse("10000000-0000-4000-8000-000000000099"),
              .type_guid = gneiss::engine::transform_component_type,
              .property_id = gneiss::engine::transform_translation_field},
      .value = {.payload = gneiss::engine::math::vector3{3.0F, 0.0F, 0.0F}},
  }};
  std::unique_ptr<gneiss::engine::prefab_binding> failed;
  if (!check_visible_prefab(native_world, render_loader, first->prefab_lease(), first->root())) {
    return 17;
  }
  if (!check_failed_camera_prefab(native_world, render_loader, prefab_loader,
                                  *first->prefab_lease().get(), invalid_overrides)) {
    return 16;
  }
  if (create_prefab_binding(native_world, render_loader, std::move(invalid_override_lease),
                            *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000005"),
                            gneiss::null_scene_node_id, first_transform, invalid_overrides,
                            failed) != gneiss::engine::error::not_found ||
      failed ||
      gneiss::engine::error{gneiss::test_support::assign_value(
          native_world.entity_count(), entity_count)} != gneiss::engine::error::success ||
      entity_count != 4U) {
    return 7;
  }

  gneiss::engine::prefab_asset_ref failure_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/lamp.prefab.json",
                                       failure_lease) != gneiss::engine::error::success) {
    return 7;
  }
  gneiss::engine::math::transform invalid_transform = {};
  invalid_transform.scale[1] = 0.0F;
  if (create_prefab_binding(native_world, render_loader, std::move(failure_lease),
                            *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000003"),
                            gneiss::null_scene_node_id, invalid_transform, no_overrides,
                            failed) != gneiss::engine::error::invalid_argument ||
      failed ||
      gneiss::engine::error{gneiss::test_support::assign_value(
          native_world.entity_count(), entity_count)} != gneiss::engine::error::success ||
      entity_count != 4U) {
    return 8;
  }

  gneiss::engine::prefab_asset_ref broken_lease;
  if (gneiss::test_support::load_asset(prefab_loader, "asset://prefabs/broken.prefab.json",
                                       broken_lease) != gneiss::engine::error::success) {
    return 9;
  }
  if (create_prefab_binding(native_world, render_loader, std::move(broken_lease),
                            *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000004"),
                            gneiss::null_scene_node_id, first_transform, no_overrides,
                            failed) != gneiss::engine::error::not_found ||
      failed ||
      gneiss::engine::error{gneiss::test_support::assign_value(
          native_world.entity_count(), entity_count)} != gneiss::engine::error::success ||
      entity_count != 4U) {
    return 10;
  }

  if (native_world.object_destroy(first->root()).failed())
    return 20;
  first.reset();
  if (gneiss::engine::error{gneiss::test_support::assign_value(
          native_world.entity_count(), entity_count)} != gneiss::engine::error::success ||
      entity_count != 2U ||
      native_world.node_get_world_transform(gneiss::scene_node_id{first_source_root},
                                            first_world) != gneiss::engine::error::invalid_handle ||
      native_world.node_get_world_transform(gneiss::scene_node_id{second_source_root},
                                            second_world) != gneiss::engine::error::success) {
    return 11;
  }
  // 来源记录不是节点所有者，析构后实体仍由场景／World 管理。
  const auto second_root = second->root();
  second.reset();
  if (gneiss::test_support::assign_value(native_world.entity_count(), entity_count) !=
          gneiss::engine::error::success ||
      entity_count != 2U)
    return 21;
  if (native_world.object_destroy(second_root).failed())
    return 22;
  if (gneiss::engine::error{gneiss::test_support::assign_value(
          native_world.entity_count(), entity_count)} != gneiss::engine::error::success ||
      entity_count != 0U) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
