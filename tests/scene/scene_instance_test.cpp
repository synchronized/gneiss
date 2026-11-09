// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/async_application.hpp"
#include "../support/value_result.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_application.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include <thread>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

gneiss::engine::result<gneiss::engine::scene_node_info>
find_info(gneiss::engine::scene_instances& scenes, gneiss::scene_instance_id scene,
          gneiss::scene_node_id node) {
  auto count = scenes.get_node_count(scene);
  if (!count)
    return count.error();
  for (std::uint64_t index = 0; index < *count; ++index) {
    auto info = scenes.get_node_info(scene, index);
    if (!info || info->node == node)
      return info;
  }
  return gneiss::engine::error::not_found;
}

std::shared_ptr<gneiss::runtime::application> create_application(std::string_view asset_root) {
  gneiss::runtime::application_configuration desc;
  desc.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  return (gneiss::test_support::assign_value(gneiss::runtime::application::create(desc),
                                             application) == gneiss::engine::error::success)
             ? application
             : nullptr;
}

} // namespace

int main() try {
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view failure_root = GNEISS_TEST_FAILURE_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing-asset.scene.json";
  constexpr std::string_view corrupt_uri = "asset://scenes/corrupt.scene.json";
  const gneiss::engine::guid triangle_uuid =
      *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000003");
  const gneiss::engine::guid camera_uuid =
      *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000002");
  const gneiss::engine::guid created_uuid =
      *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000004");
  const gneiss::engine::guid generic_uuid =
      *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000005");
  constexpr std::string_view mesh_uri = "asset://models/triangle.mesh.json";
  constexpr std::string_view material_uri = "asset://materials/triangle.material.json";
  constexpr std::string_view missing_mesh_uri = "asset://models/missing.mesh.json";
  const auto application = create_application(asset_root);
  const auto second_application = create_application(asset_root);
  gneiss::scene_instance_id scene = {};
  gneiss::scene_node_id triangle;
  gneiss::scene_node_id camera_node;
  std::uint64_t entity_count = 0;
  std::uint64_t node_count = 0;
  if (application == nullptr || second_application == nullptr ||
      gneiss::test_support::load_scene(*application, scene_uri, &scene) !=
          gneiss::engine::error::success ||
      scene == gneiss::scene_instance_id{} ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 2U ||
      gneiss::test_support::take_value(application->scenes()->find_node(scene, triangle_uuid),
                                       triangle) != gneiss::engine::error::success ||
      triangle == gneiss::null_scene_node_id ||
      gneiss::test_support::take_value(application->scenes()->find_node(scene, camera_uuid),
                                       camera_node) != gneiss::engine::error::success) {
    return 1;
  }
  // 通用应用不依赖编辑器或 Prefab 状态，空基线创建，后续更新保持存活实体。
  {
    namespace e = gneiss::engine;
    auto& world = *application->native_world();
    auto& scenes = *application->scenes();
    auto empty = scenes.create_empty(e::guid::generate());
    if (!empty)
      return 100;
    std::map<e::object_address, e::scene_object_association> bindings;
    e::composed_scene initial;
    e::composed_object parent, child;
    parent.address.object = e::guid::generate();
    parent.effective.name = "parent";
    child.address.object = e::guid::generate();
    child.parent = parent.address;
    child.effective.name = "child";
    // 输入无需父先子后。
    initial.objects = {child, parent};
    auto applied = e::apply_scene(world, scenes, *empty, bindings, {}, initial, {});
    if (!applied || !*applied || bindings.size() != 2)
      return 101;
    const auto child_id = bindings.at(child.address).entity;
    const auto parent_id = bindings.at(parent.address).entity;
    auto next = initial;
    next.objects[0].parent = {};
    next.objects[0].effective.name = "renamed";
    next.objects[0].effective.transform.translation.x = 3;
    next.objects[1].parent = child.address;
    applied = e::apply_scene(world, scenes, *empty, bindings, initial, next, {});
    if (!applied || !*applied || bindings.at(child.address).entity != child_id ||
        bindings.at(parent.address).entity != parent_id)
      return 102;
    auto actual = world.object(child_id);
    if (!actual || actual->identity()->name != "renamed" ||
        actual->local_transform()->translation.x != 3)
      return 103;
    auto cyclic = next;
    cyclic.objects[0].parent = parent.address;
    applied = e::apply_scene(world, scenes, *empty, bindings, next, cyclic, {});
    if (applied || applied.error() != e::error::invalid_argument)
      return 104;
    e::error threaded;
    std::thread worker([&] {
      auto result = e::apply_scene(world, scenes, *empty, bindings, next, next, {});
      threaded = result ? e::error::success : result.error();
    });
    worker.join();
    if (threaded != e::error::invalid_state)
      return 105;
    applied = e::apply_scene(*second_application->native_world(), scenes, *empty, bindings, next,
                             next, {});
    if (applied || applied.error() != e::error::invalid_argument)
      return 106;
    auto invalid_bindings = bindings;
    invalid_bindings.at(parent.address) = invalid_bindings.at(child.address);
    applied = e::apply_scene(world, scenes, *empty, invalid_bindings, next, next, {});
    if (applied || applied.error() != e::error::invalid_argument)
      return 109;
    auto stale = next;
    stale.objects[0].effective.transform.translation.x = 99;
    applied = e::apply_scene(world, scenes, *empty, bindings, stale, next, {});
    if (!applied || *applied || actual->local_transform()->translation.x != 3)
      return 110;
    auto reduced = next;
    reduced.objects.pop_back();
    applied = e::apply_scene(world, scenes, *empty, bindings, next, reduced, {});
    if (!applied || !*applied || bindings.size() != 1 || world.object(parent_id) ||
        bindings.at(child.address).entity != child_id)
      return 107;
    if (scenes.unload(*empty).failed() || world.object(child_id))
      return 108;
  }
  gneiss::engine::scene_node_info camera_info;
  gneiss::engine::scene_node_info triangle_info;
  if (gneiss::test_support::take_value(application->scenes()->get_node_count(scene), node_count) !=
          gneiss::engine::error::success ||
      node_count != 2U ||
      gneiss::test_support::take_value(application->scenes()->get_node_info(scene, 0U),
                                       camera_info) != gneiss::engine::error::success ||
      camera_info.node != camera_node || camera_info.parent != gneiss::null_scene_node_id ||
      camera_info.uuid != camera_uuid || camera_info.name != "Camera" ||
      !camera_info.components.get<gneiss::engine::camera_description>() ||
      !camera_info.components.get<gneiss::engine::camera_description>()->is_primary ||
      gneiss::test_support::take_value(application->scenes()->get_node_info(scene, 1U),
                                       triangle_info) != gneiss::engine::error::success ||
      triangle_info.node != triangle || triangle_info.parent != camera_node ||
      triangle_info.uuid != triangle_uuid || !triangle_info.name.empty() ||
      triangle_info.components.get<gneiss::engine::mesh_renderer_description>()->mesh_uri !=
          mesh_uri ||
      triangle_info.components.get<gneiss::engine::mesh_renderer_description>()->material_uri !=
          material_uri ||
      !triangle_info.components.get<gneiss::engine::mesh_renderer_description>() ||
      gneiss::test_support::take_value(application->scenes()->get_node_info(scene, 2U),
                                       triangle_info) != gneiss::engine::error::not_found ||
      gneiss::test_support::take_value(second_application->scenes()->get_node_count(scene),
                                       node_count) != gneiss::engine::error::invalid_handle) {
    return 2;
  }
  gneiss::engine::scene_node_info invalid_info;
  if (gneiss::test_support::take_value(
          application->scenes()->get_node_info(gneiss::scene_instance_id{}, 0U), invalid_info) !=
      gneiss::engine::error::invalid_handle) {
    return 3;
  }
  // 失败的资源选择不提交；检查实际引用和组件，而不是将 World 编码成文件。
  auto accepted_binding = application->scenes()->capture_mesh_binding(scene, triangle);
  if (!accepted_binding || application->scenes()
                               ->set_mesh_renderer(scene, triangle, missing_mesh_uri, material_uri)
                               .ok())
    return 30;
  auto after_binding = application->scenes()->capture_mesh_binding(scene, triangle);
  if (!after_binding || after_binding->mesh != accepted_binding->mesh ||
      after_binding->material != accepted_binding->material)
    return 30;
  // 通过 World 创建的子对象继承父对象的场景归属。
  const auto temporary_uuid = gneiss::engine::guid::from_bytes({91, 82, 1});
  auto temporary =
      application->native_world()->object_create(camera_node, {temporary_uuid, "Temporary"}, {});
  if (!temporary || *application->scenes()->get_node_count(scene) != 3U)
    return 31;
  if (application->native_world()->node_reparent(triangle, *temporary).failed() ||
      application->native_world()->node_reparent(triangle, camera_node).failed() ||
      application->native_world()->object_destroy(*temporary).failed())
    return 34;
  // 同一来源的另一个 World 可以独立修改，原 World 的身份不受影响。
  auto camera_before = application->native_world()->node_identity(camera_node);
  if (!camera_before)
    return 35;
  const auto camera_name = camera_before->name;
  gneiss::scene_instance_id play_scene;
  gneiss::scene_node_id play_camera;
  if (gneiss::test_support::load_scene(*second_application, scene_uri, &play_scene).failed() ||
      gneiss::test_support::take_value(
          second_application->scenes()->find_node(play_scene, camera_uuid), play_camera)
          .failed() ||
      second_application->native_world()
          ->object(gneiss::entity_id{play_camera.get()})
          ->set_name("Play Camera")
          .failed() ||
      application->native_world()->node_identity(camera_node)->name != camera_name ||
      second_application->scenes()->unload(play_scene).failed())
    return 35;

  gneiss::engine::mesh_renderer_node_creation create_desc;
  create_desc.parent = camera_node;
  create_desc.uuid = created_uuid;
  create_desc.name = "Created Mesh";
  create_desc.mesh_uri = mesh_uri;
  create_desc.material_uri = material_uri;
  gneiss::scene_node_id created_node;
  if (gneiss::test_support::take_value(
          application->scenes()->create_mesh_renderer_node(scene, create_desc), created_node) !=
          gneiss::engine::error::success ||
      created_node == gneiss::null_scene_node_id ||
      gneiss::test_support::take_value(application->scenes()->get_node_count(scene), node_count) !=
          gneiss::engine::error::success ||
      node_count != 3U ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 3U ||
      gneiss::test_support::take_value(find_info(*application->scenes(), scene, created_node),
                                       triangle_info) != gneiss::engine::error::success ||
      triangle_info.node != created_node || triangle_info.parent != camera_node ||
      triangle_info.name != "Created Mesh") {
    return 4;
  }
  // 直接修改 ECS 后，场景绑定查询必须立即反映组件状态，不能读取实例列表副本。
  {
    auto renderer = application->native_world()->node_mesh_renderer(created_node);
    gneiss::entity_id entity;
    gneiss::engine::scene_mesh_binding binding;
    if (!renderer || application->native_world()->node_get_entity(created_node, entity).failed() ||
        application->native_world()->entity_remove_mesh_renderer(entity).failed() ||
        gneiss::test_support::assign_value(
            application->scenes()->capture_mesh_binding(scene, created_node), binding)
            .failed() ||
        binding.mesh || binding.material ||
        application->native_world()->entity_set_mesh_renderer(entity, *renderer).failed())
      return 40;
  }
  gneiss::engine::camera_description author_camera{{1.04719755F, 0.2F, 1000.0F}, false};
  author_camera.settings.near = -1.0F;
  if (application->scenes()->set_camera(scene, created_node, author_camera) !=
          gneiss::engine::error::invalid_argument ||
      application->scenes()->set_camera(gneiss::scene_instance_id{}, created_node, author_camera) !=
          gneiss::engine::error::invalid_handle ||
      application->scenes()->set_camera(scene, gneiss::scene_node_id{UINT64_MAX}, author_camera) !=
          gneiss::engine::error::invalid_handle) {
    return 34;
  }
  author_camera.settings.near = 0.2F;
  if (application->scenes()->set_camera(scene, created_node, author_camera) !=
          gneiss::engine::error::success ||
      application->scenes()->remove_camera(scene, created_node) != gneiss::engine::error::success ||
      application->scenes()->remove_camera(scene, created_node) !=
          gneiss::engine::error::not_found ||
      application->scenes()->remove_mesh_renderer(scene, created_node) !=
          gneiss::engine::error::success ||
      application->scenes()->remove_mesh_renderer(scene, created_node) !=
          gneiss::engine::error::not_found) {
    return 27;
  }
  if (application->scenes()->set_mesh_renderer(scene, created_node, mesh_uri, material_uri) !=
      gneiss::engine::error::success) {
    return 28;
  }
  gneiss::engine::node_creation generic_desc;
  generic_desc.uuid = generic_uuid;
  generic_desc.name = "Empty";
  generic_desc.local_transform.translation[1] = 2.0F;
  gneiss::scene_node_id generic_node;
  if (gneiss::test_support::take_value(application->scenes()->create_node(scene, generic_desc),
                                       generic_node) != gneiss::engine::error::success ||
      application->native_world()
              ->object(gneiss::entity_id{generic_node.get()})
              ->set_name("Group") != gneiss::engine::error::success ||
      application->scenes()->reparent_node(scene, triangle, generic_node) !=
          gneiss::engine::error::success ||
      application->scenes()->reparent_node(scene, generic_node, triangle) !=
          gneiss::engine::error::invalid_argument ||
      gneiss::test_support::take_value(find_info(*application->scenes(), scene, generic_node),
                                       triangle_info) != gneiss::engine::error::success ||
      triangle_info.node != generic_node || triangle_info.name != "Group" ||
      triangle_info.local_transform.translation[1] != 2.0F || !triangle_info.components.empty()) {
    return 13;
  }
  // 运行删除递归清理成员；重建消费显式描述和资源引用，不从 World 保存作者快照。
  if (application->scenes()->reparent_node(scene, generic_node, camera_node).failed())
    return 29;
  auto retained = application->scenes()->capture_mesh_binding(scene, triangle);
  if (!retained)
    return 15;
  const auto old_generic_node = generic_node;
  const auto old_triangle_node = triangle;
  if (application->scenes()->destroy_subtree(scene, generic_node).failed())
    return 15;
  if (application->native_world()->object(gneiss::entity_id{old_generic_node.get()}) ||
      application->native_world()->object(gneiss::entity_id{old_triangle_node.get()}))
    return 17;
  generic_desc.name = "Group";
  generic_desc.parent = camera_node;
  auto recreated = application->scenes()->create_node(scene, generic_desc);
  if (!recreated || *recreated == old_generic_node)
    return 18;
  generic_node = *recreated;
  auto child_desc = create_desc;
  child_desc.uuid = triangle_uuid;
  child_desc.name = "Triangle";
  child_desc.parent = generic_node;
  auto rebuilt =
      application->scenes()->create_mesh_renderer_node_ready(scene, child_desc, *retained);
  if (!rebuilt || *rebuilt == old_triangle_node)
    return 20;
  triangle = *rebuilt;
  auto rebuilt_binding = application->scenes()->capture_mesh_binding(scene, triangle);
  if (!rebuilt_binding || rebuilt_binding->mesh != retained->mesh ||
      rebuilt_binding->material != retained->material)
    return 21;
  if (application->scenes()->reparent_node(scene, triangle, camera_node) !=
      gneiss::engine::error::success) {
    return 24;
  }
  if (gneiss::test_support::take_value(
          application->scenes()->create_mesh_renderer_node(scene, create_desc), triangle) !=
      gneiss::engine::error::invalid_argument) {
    return 5;
  }
  if (application->scenes()->set_mesh_renderer(scene, created_node, missing_mesh_uri,
                                               material_uri) != gneiss::engine::error::not_found) {
    return 6;
  }
  gneiss::entity_id camera_entity;
  gneiss::engine::math::transform transform;
  transform.translation[0] = 5.0F;
  gneiss::engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  camera.near = 0.25F;
  if (application->native_world()->node_get_entity(camera_node, camera_entity) !=
          gneiss::engine::error::success ||
      application->native_world()->entity_set_local_transform(camera_entity, transform) !=
          gneiss::engine::error::success ||
      application->native_world()->entity_configure_camera(camera_entity, camera) !=
          gneiss::engine::error::success ||
      application->native_world()
          ->node_set_identity(camera_node, {camera_uuid, "Runtime Camera"})
          .failed() ||
      !application->native_world()->node_identity(camera_node)) {
    return 7;
  }
  auto runtime_camera = application->scenes()->get_node_info(scene, 0);
  auto runtime_mesh = application->native_world()->node_identity(created_node);
  auto runtime_group = application->native_world()->node_identity(generic_node);
  if (!runtime_camera || runtime_camera->node != camera_node ||
      runtime_camera->name != "Runtime Camera" ||
      runtime_camera->local_transform.translation.x != 5.0F ||
      !runtime_camera->components.get<gneiss::engine::camera_description>() ||
      runtime_camera->components.get<gneiss::engine::camera_description>()->settings.near !=
          0.25F ||
      !runtime_mesh || runtime_mesh->uuid != created_uuid || runtime_mesh->name != "Created Mesh" ||
      !runtime_group || runtime_group->name != "Group" ||
      second_application->scenes()->get_node_count(scene).error() !=
          gneiss::engine::error::invalid_handle)
    return 8;
  if (application->scenes()->destroy_node(scene, camera_node) !=
          gneiss::engine::error::invalid_state ||
      application->scenes()->destroy_node(scene, created_node) != gneiss::engine::error::success ||
      application->scenes()->destroy_node(scene, generic_node) != gneiss::engine::error::success ||
      application->scenes()->destroy_node(scene, created_node) !=
          gneiss::engine::error::invalid_handle ||
      gneiss::test_support::take_value(application->scenes()->get_node_count(scene), node_count) !=
          gneiss::engine::error::success ||
      node_count != 2U ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 2U) {
    return 9;
  }
  const gneiss::engine::guid empty_uuid =
      *gneiss::engine::guid::parse("00000000-0000-4000-8000-000000000099");
  gneiss::scene_instance_id empty_scene = gneiss::scene_instance_id{};
  std::uint64_t empty_count = 1U;
  if (gneiss::test_support::take_value(application->scenes()->create_empty({}), empty_scene) !=
          gneiss::engine::error::invalid_argument ||
      empty_scene != gneiss::scene_instance_id{} ||
      gneiss::test_support::take_value(application->scenes()->create_empty(empty_uuid),
                                       empty_scene) != gneiss::engine::error::success ||
      gneiss::test_support::take_value(application->scenes()->get_node_count(empty_scene),
                                       empty_count) != gneiss::engine::error::success ||
      empty_count != 0U ||
      application->scenes()->unload(empty_scene) != gneiss::engine::error::success) {
    return 13;
  }
  if (second_application->scenes()->unload(scene) != gneiss::engine::error::invalid_handle ||
      application->scenes()->unload(scene) != gneiss::engine::error::success ||
      application->scenes()->unload(scene) != gneiss::engine::error::invalid_handle ||
      gneiss::test_support::assign_value(application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 0U) {
    return 10;
  }
  if (second_application->shutdown() != gneiss::engine::error::success ||
      application->shutdown() != gneiss::engine::error::success) {
    return 11;
  }

  const auto failing_application = create_application(failure_root);
  scene = gneiss::scene_instance_id{};
  if (failing_application == nullptr ||
      gneiss::test_support::load_scene(*failing_application, missing_uri, &scene) !=
          gneiss::engine::error::not_found ||
      scene != gneiss::scene_instance_id{} ||
      gneiss::test_support::load_scene(*failing_application, corrupt_uri, &scene) !=
          gneiss::engine::error::invalid_argument ||
      scene != gneiss::scene_instance_id{} ||
      gneiss::test_support::take_value(failing_application->scenes()->create_empty(empty_uuid),
                                       scene) != gneiss::engine::error::success ||
      failing_application->scenes()->unload(scene) != gneiss::engine::error::success ||
      gneiss::test_support::assign_value(failing_application->native_world()->entity_count(),
                                         entity_count) != gneiss::engine::error::success ||
      entity_count != 0U || failing_application->shutdown() != gneiss::engine::error::success) {
    return 12;
  }
  return 0;
} catch (...) {
  return 99;
}
