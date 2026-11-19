// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_application.hpp"
#include "../../support/mesh_objects.hpp"
#include "../../support/value_result.hpp"
#include <algorithm>
#include <gneiss/engine/core/base/guid.hpp>
#include <gneiss/engine/core/base/rid.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/function/scene/scene_update.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <thread>

#include <cstdint>
#include <string>
#include <string_view>

namespace {
std::shared_ptr<gneiss::runtime::application> create_application(std::string_view asset_root) {
  gneiss::runtime::application_configuration desc;
  desc.executor = &gneiss::test_support::application_tasks();
  desc.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  return (gneiss::test_support::assign_value(gneiss::runtime::application::create(desc),
                                             application) == gneiss::error::success)
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
  const gneiss::guid triangle_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000003");
  const gneiss::guid camera_uuid = *gneiss::guid::parse("00000000-0000-4000-8000-000000000002");
  constexpr std::string_view mesh_uri = "asset://models/triangle.mesh.json";
  constexpr std::string_view material_uri = "asset://materials/triangle.material.json";
  constexpr std::string_view missing_mesh_uri = "asset://models/missing.mesh.json";
  const auto application = create_application(asset_root);
  const auto second_application = create_application(asset_root);
  auto primary_world = gneiss::create_native();
  auto secondary_world = gneiss::create_native();
  if (!primary_world || !secondary_world)
    return 1;
  std::vector<gneiss::scene_object_binding> scene_objects;
  gneiss::scene_instance scene = {};
  gneiss::entity_id triangle;
  gneiss::entity_id camera_node;
  if (application == nullptr || second_application == nullptr ||
      gneiss::test_support::load_scene(*application, **primary_world, scene_uri, &scene, nullptr,
                                       false, &scene_objects) != gneiss::error::success ||
      !scene.is_valid() || (*primary_world)->entity_count() != 2U)
    return 1;
  for (const auto& binding : scene_objects) {
    if (binding.source)
      continue;
    if (binding.object == triangle_uuid)
      triangle = binding.entity;
    if (binding.object == camera_uuid)
      camera_node = binding.entity;
  }
  auto& owner = **primary_world;
  const auto original_camera = owner.object(camera_node);
  const auto original_triangle = owner.object(triangle);
  if (!original_camera || !original_triangle)
    return 1;
  // 通用应用不依赖编辑器或 Prefab 状态，空基线创建，后续更新保持存活实体。
  {
    namespace e = gneiss;
    auto& world = **primary_world;
    auto empty = world.create_scene();
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
    auto applied = e::apply_scene(*empty, bindings, {}, initial, {});
    if (!applied || !*applied || bindings.size() != 2)
      return 101;
    const auto child_id = bindings.at(child.address).entity;
    const auto parent_id = bindings.at(parent.address).entity;
    auto next = initial;
    next.objects[0].parent = {};
    next.objects[0].effective.name = "renamed";
    next.objects[0].effective.transform.translation.x = 3;
    next.objects[1].parent = child.address;
    applied = e::apply_scene(*empty, bindings, initial, next, {});
    if (!applied || !*applied || bindings.at(child.address).entity != child_id ||
        bindings.at(parent.address).entity != parent_id)
      return 102;
    auto actual = world.object(child_id);
    if (!actual || actual->name() != "renamed" || actual->local_transform().translation.x != 3)
      return 103;
    auto cyclic = next;
    cyclic.objects[0].parent = parent.address;
    applied = e::apply_scene(*empty, bindings, next, cyclic, {});
    if (applied || applied.error() != e::error::invalid_argument)
      return 104;
    e::error threaded;
    std::thread worker([&] {
      auto result = e::apply_scene(*empty, bindings, next, next, {});
      threaded = result ? e::error::success : result.error();
    });
    worker.join();
    if (threaded != e::error::invalid_state)
      return 105;
    auto foreign_scene = (*secondary_world)->create_scene();
    if (!foreign_scene)
      return 106;
    applied = e::apply_scene(*foreign_scene, bindings, next, next, {});
    // 其他 World 的关联不匹配目标场景；不创建或删除对象。
    if (!applied || *applied || foreign_scene->size() != 0)
      return 106;
    auto invalid_bindings = bindings;
    invalid_bindings.at(parent.address) = invalid_bindings.at(child.address);
    applied = e::apply_scene(*empty, invalid_bindings, next, next, {});
    if (applied || applied.error() != e::error::invalid_argument)
      return 109;
    auto stale = next;
    stale.objects[0].effective.transform.translation.x = 99;
    applied = e::apply_scene(*empty, bindings, stale, next, {});
    if (!applied || *applied || actual->local_transform().translation.x != 3)
      return 110;
    auto reduced = next;
    reduced.objects.pop_back();
    applied = e::apply_scene(*empty, bindings, next, reduced, {});
    if (!applied || !*applied || bindings.size() != 1 || world.object(parent_id) ||
        bindings.at(child.address).entity != child_id)
      return 107;
    empty->unload();
    if (world.object(child_id))
      return 108;
  }
  const auto camera_components = gneiss::capture_components(owner, camera_node);
  const auto triangle_components = gneiss::capture_components(owner, triangle);
  if (!camera_components || !triangle_components)
    return 2;
  const auto* camera_description = camera_components->get<gneiss::camera_description>();
  const auto* mesh_description = triangle_components->get<gneiss::mesh_component_description>();
  if (scene.size() != 2U || original_camera->parent().is_valid() ||
      original_camera->name() != "Camera" || !camera_description ||
      !camera_description->is_primary || original_triangle->parent() != *original_camera ||
      !original_triangle->name().empty() || !mesh_description ||
      mesh_description->mesh_uri != mesh_uri || mesh_description->material_uri != material_uri)
    return 2;
  if (gneiss::scene_instance::borrow(**secondary_world, scene.id()).error() !=
          gneiss::error::invalid_handle ||
      gneiss::scene_instance::borrow(owner, {}).error() != gneiss::error::invalid_handle)
    return 3;
  // 失败的资源选择不提交；检查实际引用和组件，而不是将 World 编码成文件。
  const auto triangle_object = (*primary_world)->find(triangle);
  const auto* accepted = triangle_object.try_component<gneiss::mesh_component>();
  if (!accepted)
    return 30;
  const auto accepted_binding = *accepted;
  if (gneiss::test_support::assign_mesh(**primary_world, application->assets(), scene, triangle,
                                        missing_mesh_uri, material_uri)
          .ok())
    return 30;
  const auto* after_binding = triangle_object.try_component<gneiss::mesh_component>();
  if (!after_binding || after_binding->mesh != accepted_binding.mesh ||
      after_binding->material != accepted_binding.material)
    return 30;
  // 场景入口创建子对象，归属必须与父对象一致。
  auto temporary_scene = gneiss::scene_instance::borrow(**primary_world, (scene).id());
  if (!temporary_scene)
    return 31;
  auto temporary = temporary_scene->create_object(
      {"Temporary", {}}, (*primary_world)->find(gneiss::entity_id{camera_node.get()}));
  if (!temporary || scene.size() != 3U)
    return 31;
  if (primary_world->get()
          ->find(gneiss::entity_id{(triangle).get()})
          .reparent((*primary_world)->find(temporary->entity()))
          .failed() ||
      primary_world->get()
          ->find(gneiss::entity_id{(triangle).get()})
          .reparent((*primary_world)->find(gneiss::entity_id{(camera_node).get()}))
          .failed())
    return 34;
  temporary->destroy();
  // 同一来源的另一个 World 可以独立修改，原 World 的身份不受影响。
  auto camera_before = (*primary_world)->find(gneiss::entity_id{(camera_node).get()}).name();
  const std::string camera_name{camera_before};
  std::vector<gneiss::scene_object_binding> play_objects;
  gneiss::scene_instance play_scene;
  gneiss::entity_id play_camera;
  if (gneiss::test_support::load_scene(*second_application, **secondary_world, scene_uri,
                                       &play_scene, nullptr, false, &play_objects)
          .failed())
    return 35;
  const auto play_binding = std::ranges::find_if(play_objects, [&](const auto& binding) {
    return binding.object == camera_uuid && !binding.source;
  });
  if (play_binding == play_objects.end())
    return 35;
  play_camera = play_binding->entity;
  (*secondary_world)->find(play_camera).set_name("Play Camera");
  if (owner.find(camera_node).name() != camera_name)
    return 35;
  play_scene.unload();

  gneiss::test_support::mesh_fixture create_desc;
  create_desc.parent = camera_node;
  create_desc.name = "Created Mesh";
  create_desc.mesh_uri = mesh_uri;
  create_desc.material_uri = material_uri;
  gneiss::entity_id created_node;
  if (gneiss::test_support::take_value(gneiss::test_support::spawn_mesh(**primary_world,
                                                                        application->assets(),
                                                                        scene, create_desc),
                                       created_node) != gneiss::error::success ||
      created_node == gneiss::null_entity_id || scene.size() != 3U || owner.entity_count() != 3U)
    return 4;
  const auto created_object = owner.find(created_node);
  if (created_object.parent() != *original_camera || created_object.name() != "Created Mesh")
    return 4;
  // 直接修改 ECS 后，场景绑定查询必须立即反映组件状态，不能读取实例列表副本。
  {
    auto renderer = primary_world->get()
                        ->find(gneiss::entity_id{(created_node).get()})
                        .try_component<gneiss::mesh_component>();
    if (!renderer)
      return 40;
    const auto saved_renderer = *renderer;
    gneiss::entity_id entity;
    if (!renderer ||
        gneiss::test_support::assign_value(
            (*primary_world)->object(gneiss::entity_id{(created_node).get()}), entity)
            .failed() ||
        primary_world->get()->find(entity).remove_component<gneiss::mesh_component>().failed() ||
        (*primary_world)->find(entity).try_component<gneiss::mesh_component>() ||
        primary_world->get()
            ->find(entity)
            .add_component<gneiss::mesh_component>(saved_renderer)
            .failed())
      return 40;
  }
  if (created_object
          .add_component<gneiss::camera_component>(
              gneiss::camera_component{{1.04719755F, 0.2F, 1000.0F}, false})
          .failed() ||
      primary_world->get()
              ->find(gneiss::entity_id{created_node.get()})
              .remove_component<gneiss::camera_component>() != gneiss::error::success ||
      primary_world->get()
              ->find(gneiss::entity_id{created_node.get()})
              .remove_component<gneiss::camera_component>() != gneiss::error::not_found ||
      primary_world->get()
              ->find(gneiss::entity_id{created_node.get()})
              .remove_component<gneiss::mesh_component>() != gneiss::error::success ||
      primary_world->get()
              ->find(gneiss::entity_id{created_node.get()})
              .remove_component<gneiss::mesh_component>() != gneiss::error::not_found) {
    return 27;
  }
  if (gneiss::test_support::assign_mesh(**primary_world, application->assets(), scene, created_node,
                                        mesh_uri, material_uri) != gneiss::error::success) {
    return 28;
  }
  auto destination = gneiss::scene_instance::borrow(**primary_world, (scene).id());
  if (!destination)
    return 13;
  gneiss::math::transform generic_transform;
  generic_transform.translation[1] = 2.0F;
  gneiss::entity_id generic_node;
  if (gneiss::test_support::assign_value(destination->create_object({"Empty", generic_transform}),
                                         generic_node) != gneiss::error::success)
    return 13;
  (*primary_world)->find(generic_node).set_name("Group");
  const auto group = owner.find(generic_node);
  if (owner.find(triangle).reparent(group).failed() ||
      group.reparent(owner.find(triangle)) != gneiss::error::invalid_argument ||
      group.name() != "Group" || group.local_transform().translation[1] != 2.0F)
    return 13;
  const auto group_components = gneiss::capture_components(owner, generic_node);
  if (!group_components || !group_components->empty())
    return 13;
  // 运行删除递归清理成员；重建消费显式描述和资源引用，不从 World 保存作者快照。
  if (owner.find(generic_node).reparent(owner.find(camera_node)).failed())
    return 29;
  const auto* retained_component =
      (*primary_world)->find(triangle).try_component<gneiss::mesh_component>();
  if (!retained_component)
    return 15;
  const auto retained = *retained_component;
  const auto old_generic_node = generic_node;
  const auto old_triangle_node = triangle;
  owner.find(generic_node).destroy();
  if ((*primary_world)->object(gneiss::entity_id{old_generic_node.get()}) ||
      (*primary_world)->object(gneiss::entity_id{old_triangle_node.get()}))
    return 17;
  auto recreated =
      destination->create_object({"Group", generic_transform}, (*primary_world)->find(camera_node));
  if (!recreated || recreated->entity() == old_generic_node)
    return 18;
  generic_node = recreated->entity();
  auto child_desc = create_desc;
  child_desc.name = "Triangle";
  child_desc.parent = generic_node;
  auto rebuilt = gneiss::test_support::spawn_mesh(**primary_world, application->assets(), scene,
                                                  child_desc, &retained);
  if (!rebuilt || *rebuilt == old_triangle_node)
    return 20;
  triangle = *rebuilt;
  const auto* rebuilt_binding =
      (*primary_world)->find(triangle).try_component<gneiss::mesh_component>();
  if (!rebuilt_binding || rebuilt_binding->mesh != retained.mesh ||
      rebuilt_binding->material != retained.material)
    return 21;
  if (owner.find(triangle).reparent(owner.find(camera_node)) != gneiss::error::success) {
    return 24;
  }
  // 同名运行对象可以重复创建，身份由实体 ID 区分。
  auto duplicate =
      gneiss::test_support::spawn_mesh(**primary_world, application->assets(), scene, create_desc);
  if (!duplicate || *duplicate == created_node)
    return 5;
  (*primary_world)->find(*duplicate).destroy();
  if (gneiss::test_support::assign_mesh(**primary_world, application->assets(), scene, created_node,
                                        missing_mesh_uri,
                                        material_uri) != gneiss::error::not_found) {
    return 6;
  }
  auto world = primary_world->get();
  auto camera_state = world->object(gneiss::entity_id{(camera_node).get()});
  if (!camera_state) {
    return 7;
  }
  auto camera_object = *camera_state;
  gneiss::entity_id camera_entity;
  gneiss::math::transform transform;
  transform.translation[0] = 5.0F;
  gneiss::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  camera.near_plane = 0.25F;
  if (gneiss::test_support::assign_value(world->object(gneiss::entity_id{(camera_node).get()}),
                                         camera_entity) != gneiss::error::success ||
      camera_object.set_local_transform(transform) != gneiss::error::success ||
      !(camera_object.try_component<gneiss::camera_component>()->settings = camera).valid() ||
      !world->find(gneiss::entity_id{(camera_node).get()}).is_valid()) {
    return 7;
  }
  camera_object.set_name("Runtime Camera");
  const auto runtime_camera = gneiss::capture_components(owner, camera_node);
  auto runtime_mesh = (*primary_world)->find(gneiss::entity_id{(created_node).get()}).name();
  auto runtime_group = (*primary_world)->find(gneiss::entity_id{(generic_node).get()}).name();
  if (!runtime_camera || camera_object.name() != "Runtime Camera" ||
      camera_object.local_transform().translation.x != 5.0F ||
      !runtime_camera->get<gneiss::camera_description>() ||
      runtime_camera->get<gneiss::camera_description>()->settings.near_plane != 0.25F ||
      runtime_mesh != "Created Mesh" || runtime_group != "Group")
    return 8;
  created_object.destroy();
  owner.find(generic_node).destroy();
  if (owner.object(created_node) || owner.object(generic_node) || scene.size() != 2U ||
      owner.entity_count() != 2U)
    return 9;
  // 原生 World 无需 attach 集合或提供描述 UUID，也能独立管理场景。
  {
    auto native = gneiss::create_native();
    auto other = gneiss::create_native();
    if (!native || !other)
      return 15;
    auto instance = (*native)->create_scene();
    if (!instance || !instance->is_valid() || instance->size() != 0U)
      return 16;
    std::map<gneiss::object_address, gneiss::scene_object_association> mapping;
    auto applied_native = gneiss::apply_scene(*instance, mapping, {}, {}, {});
    if (!applied_native || !*applied_native)
      return 19;
    auto object = instance->create_object({"Direct", {}});
    if (!object || !instance->contains(*object) || instance->contains({}) ||
        instance->size() != 1U ||
        gneiss::scene_instance::borrow(**other, instance->id()).error() !=
            gneiss::error::invalid_handle)
      return 17;
    auto target_scene = (*native)->create_scene();
    if (!target_scene)
      return 20;
    auto target_parent = target_scene->create_object({"Parent", {}});
    gneiss::math::transform root_transform;
    root_transform.scale = {2.0F, 1.0F, 1.0F};
    gneiss::math::transform child_transform;
    child_transform.rotation = {0.9238795F, 0.0F, 0.0F, 0.3826834F};
    if (!target_parent || object->set_local_transform(root_transform).failed())
      return 21;
    auto child = instance->create_object({"Child", child_transform}, *object);
    if (!child)
      return 22;
    auto matrix = child->world_matrix();
    if (matrix !=
        gneiss::math::model_matrix(root_transform) * gneiss::math::model_matrix(child_transform))
      return 23;
    if ((*native)->transfer_subtree(*object, *instance, *child) !=
            gneiss::error::invalid_argument ||
        instance->size() != 2U)
      return 24;
    if ((*native)->transfer_subtree(*object, *target_scene, *target_parent).failed() ||
        instance->size() != 0U || target_scene->size() != 3U ||
        child->scene().id() != target_scene->id() ||
        object->parent().entity() != target_parent->entity())
      return 25;
    unsigned visited = 0;
    if (target_scene
            ->visit_objects(
                [&](auto) {
                  ++visited;
                  return gneiss::error::success;
                },
                true)
            .failed() ||
        visited != 1U)
      return 26;
    visited = 0;
    if (target_scene->visit_objects([&](auto) {
          ++visited;
          return gneiss::error::io;
        }) != gneiss::error::io ||
        visited != 1U)
      return 27;
    // 原场景卸载不能删除已迁出的对象；目标场景卸载统一回收整棵子树。
    instance->unload();
    if (!object->is_valid() || !child->is_valid())
      return 28;
    instance = std::move(target_scene);
    gneiss::error worker_status;
    std::thread worker(
        [&] { worker_status = gneiss::scene_instance::borrow(**native, instance->id()).error(); });
    worker.join();
    if (worker_status != gneiss::error::invalid_state)
      return 18;
    instance->unload();
    if (instance->is_valid() || object->is_valid())
      return 18;
  }
  auto empty_scene = owner.create_scene();
  if (!empty_scene || empty_scene->size() != 0U)
    return 13;
  empty_scene->unload();
  // 槽位复用不能恢复旧场景身份；外部 ID 需重新借用并校验 generation。
  auto reused_scene = owner.create_scene();
  if (!reused_scene || *reused_scene == *empty_scene || empty_scene->is_valid() ||
      gneiss::scene_instance::borrow(owner, empty_scene->id()).error() !=
          gneiss::error::invalid_handle ||
      reused_scene->size() != 0U)
    return 14;
  reused_scene->unload();
  scene.unload();
  if (scene.is_valid() || camera_object.is_valid() || owner.entity_count() != 0U)
    return 10;
  second_application->request_exit();
  application->request_exit();
  if (second_application->run().failed() || application->run().failed()) {
    return 11;
  }

  const auto failing_application = create_application(failure_root);
  auto failure_world = gneiss::create_native();
  if (!failure_world)
    return 12;
  scene = gneiss::scene_instance{};
  if (failing_application == nullptr ||
      gneiss::test_support::load_scene(*failing_application, **failure_world, missing_uri,
                                       &scene) != gneiss::error::not_found ||
      scene != gneiss::scene_instance{} ||
      gneiss::test_support::load_scene(*failing_application, **failure_world, corrupt_uri,
                                       &scene) != gneiss::error::invalid_argument ||
      scene != gneiss::scene_instance{}) {
    return 12;
  }
  auto recovered = (*failure_world)->create_scene();
  if (!recovered)
    return 12;
  recovered->unload();
  failing_application->request_exit();
  if ((*failure_world)->entity_count() != 0U || failing_application->run().failed())
    return 12;
  return 0;
} catch (...) {
  return 99;
}
