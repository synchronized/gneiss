// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/movement_component.hpp"
#include "../support/value_result.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/scene_instance.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_file.hpp"
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <thread>
namespace e = gneiss::engine;
namespace {
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("组件适配测试失败，行=" + std::to_string(location.line()));
}
using namespace gneiss::test_support;
struct unknown_description
    : e::meta::object_type<unknown_description, e::component_description_base> {
  inline static constexpr e::guid type_guid = e::guid::from_bytes({84, 2});
};
e::scene_instantiation_data prepare(e::scene_description scene, e::prefab_asset_ref prefab = {}) {
  e::scene_preparation_inputs input;
  input.scene = e::asset_ref<const e::scene_description>{
      std::make_shared<const e::scene_description>(std::move(scene))};
  if (prefab)
    input.prefabs.emplace("asset://test.prefab", std::move(prefab));
  e::scene_instantiation_data output;
  e::scene_diagnostic diagnostic;
  check(e::prepare_loaded_scene(input, {}, output, diagnostic, {}).ok());
  check(output.assets.empty());
  return output;
}
}
int main() try {
  check(e::initialize_meta(register_movement_meta).ok());
  check(e::register_scene_component(e::component_adapter{}) == e::error::invalid_argument);
  auto custom = operations();
  const auto alias = e::guid::from_bytes({84, 99});
  custom.property_type = alias;
  check(e::register_scene_component(custom).ok());
  check(e::register_scene_component(operations()) == e::error::invalid_argument);
  check(e::component_description_type(alias) == movement_description::type_guid);
  check(e::component_description_type(movement_description::type_guid) ==
        movement_description::type_guid);
  check(e::component_description_type(e::camera_description::type_guid) ==
        e::camera_description::type_guid);
  check(e::component_description_type(unknown_description::type_guid) ==
        unknown_description::type_guid);
  check(e::component_editor(movement_description::type_guid)->route ==
        e::component_editor_route::reflected);
  check(e::component_editor(e::mesh_renderer_description::type_guid)->route ==
        e::component_editor_route::mesh_assets);
  const auto primary =
      e::meta::describe_property(e::camera_description::type_guid, e::meta::property_id{4});
  check(primary && e::meta::has_flags(primary->flags, e::meta::property_flags::editor_read_only));
  check(!e::meta::has_flags(primary->flags, e::meta::property_flags::read_only));
  e::camera_description camera{{1, 0.1F, 100}, true};
  auto saved_camera = gneiss::engine::serialization::json::serialize_bytes(camera);
  check(saved_camera.has_value());
  auto loaded_camera =
      gneiss::engine::serialization::json::deserialize<e::camera_description>(*saved_camera);
  check(loaded_camera && loaded_camera->is_primary);
  auto files = std::make_shared<e::files::service>();
  e::asset_manager assets(files);
  auto owner = e::create_native();
  check(owner.has_value());
  auto& world = **owner;
  auto& resources = assets;
  auto attached = e::scene_instances::attach(world, resources);
  check(attached.has_value());
  auto& scenes = **attached;
  check(world.scenes() == &scenes);
  auto duplicate = e::scene_instances::attach(world, resources);
  check(!duplicate && duplicate.error() == e::error::invalid_state);
  check(!e::scene_instance{}.is_valid());
  e::scene_description source;
  source.uuid = e::guid::from_bytes({1});
  e::game_object_description first;
  first.uuid = e::guid::from_bytes({2});
  first.components.set(movement_description{2});
  first.components.set(e::mesh_renderer_description{});
  source.objects.push_back(first);
  auto second = first;
  second.uuid = e::guid::from_bytes({3});
  second.parent_uuid = first.uuid;
  source.objects.push_back(second);
  // 真实文件格式解码后进入统一准备与同步实例化路径。
  auto encoded = gneiss::engine::asset_file::serialize_bytes(source);
  check(encoded.has_value());
  auto decoded = gneiss::engine::asset_file::deserialize<e::scene_description>(*encoded);
  check(decoded.has_value());
  auto prepared = prepare(*decoded);
  check(e::register_scene_component(operations()) == e::error::invalid_state);
  auto created = scenes.instantiate_prepared(std::move(prepared), {});
  check(created.has_value());
  check(created->objects.size() == 2);
  check(created->objects[0].object == first.uuid && !created->objects[0].source);
  check(created->objects[0].entity.get() == created->objects[0].node.get());
  auto instance = created->instance;
  auto borrowed = e::scene_instance::borrow(world, instance);
  check(borrowed && borrowed->is_valid());
  auto member_count = borrowed->size();
  check(member_count && *member_count == 2);
  auto borrowed_copy = *borrowed;
  auto another_scene = scenes.instantiate_prepared(prepare(source), {});
  check(another_scene.has_value());
  auto another_handle = e::scene_instance::borrow(world, another_scene->instance);
  check(another_handle.has_value());
  auto first_object = borrowed->find(first.uuid);
  auto foreign_parent = another_handle->find(first.uuid);
  check(first_object && foreign_parent);
  check(first_object->reparent(*foreign_parent) == e::error::invalid_argument);
  check(world.node_reparent(created->objects[0].node, another_scene->objects[0].node) ==
        e::error::invalid_argument);
  auto unchanged_parent = first_object->parent();
  check(unchanged_parent && !unchanged_parent->is_valid());
  // 不能通过未登记的中间对象绕开不同场景的成员边界。
  auto bridge = world.create_object({e::guid::from_bytes({9}), "bridge"});
  check(bridge.has_value());
  check(first_object->reparent(*bridge).ok());
  check(bridge->reparent(*foreign_parent) == e::error::invalid_argument);
  check(first_object->reparent({}).ok());
  check(bridge->destroy().ok());
  auto parent_bridge =
      world.create_object({e::guid::from_bytes({10}), "parent bridge"}, {}, *foreign_parent);
  check(parent_bridge.has_value());
  check(first_object->reparent(*parent_bridge) == e::error::invalid_argument);
  check(another_handle->unload().ok());
  check(first_object->is_valid());
  auto other_world = e::create_native();
  check(other_world.has_value());
  check(e::scene_instances::attach(**other_world, resources).has_value());
  check(!e::scene_instance::borrow(**other_world, instance));
  e::error wrong_thread;
  std::thread scene_worker([&] { wrong_thread = borrowed_copy.unload(); });
  scene_worker.join();
  check(wrong_thread == e::error::invalid_state);
  check(borrowed_copy.is_valid());
  auto found = scenes.find_node(instance, first.uuid);
  check(found.has_value());
  auto node = *found;
  auto queried = scenes.get_node_info(instance, 0);
  check(queried && queried->components.get<movement_description>() &&
        queried->components.get<movement_description>()->speed == 2);
  auto invalid = scenes.get_node_count({});
  check(!invalid && invalid.error() == e::error::invalid_handle);
  auto missing = scenes.find_node(instance, {});
  check(!missing && missing.error() == e::error::not_found);
  auto captured = e::capture_components(world, node);
  check(bool(captured));
  check(captured->identity(movement_description::type_guid) ==
        first.components.identity(movement_description::type_guid));
  auto id = entity(world, node);
  auto* component = state(world).get<movement_component>(id);
  check(component && component->data.speed == 2);
  component->data.speed = 4;
  check(e::edit_runtime_component({world, node}, movement_description::type_guid,
                                  e::meta::property_id{1}, e::value{5.0F})
            .ok());
  check(state(world).get<movement_component>(id)->data.speed == 5);
  check(e::edit_runtime_component({world, node}, movement_description::type_guid,
                                  e::meta::property_id{1},
                                  e::value{-1.0F}) == e::error::invalid_argument);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  // 文件往返使用调用方描述，显式采集需要保留的组件，不捕获整个运行场景。
  e::scene_description saved = source;
  for (auto& object : saved.objects) {
    auto member = scenes.find_node(instance, object.uuid);
    check(bool(member));
    auto components = e::capture_components(world, *member);
    check(bool(components));
    object.components = std::move(*components);
  }
  check(saved.objects[0].components.get<movement_description>()->speed == 5);
  check(saved.objects[0].components.identity(movement_description::type_guid) ==
        first.components.identity(movement_description::type_guid));
  auto saved_bytes = gneiss::engine::asset_file::serialize_bytes(saved);
  check(saved_bytes.has_value());
  auto restored = gneiss::engine::asset_file::deserialize<e::scene_description>(*saved_bytes);
  check(restored.has_value());
  state(world).get<movement_component>(id)->data.speed = 8;
  check(scenes.reload_loaded(instance, *restored).ok());
  check(state(world).get<movement_component>(id)->data.speed == 5);
  // 第二个安装失败：第一个对象的已应用修改也必须恢复。
  auto failing = *restored;
  failing.objects[0].components.set(movement_description{7});
  failing.objects[1].components.set(movement_description{13});
  check(scenes.reload_loaded(instance, failing) == e::error::invalid_state);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  e::error foreign;
  std::thread worker([&] {
    auto captured = e::capture_components(world, node);
    foreign = captured ? e::error::success : captured.error();
  });
  worker.join();
  check(foreign == e::error::invalid_state);
  check(!e::capture_components(world, {}));
  e::component_descriptions unknown;
  unknown.set(unknown_description{});
  check(e::validate_components(unknown) == e::error::unsupported);
  std::weak_ptr<int> lifetime = state(world).get<movement_component>(id)->lifetime;
  restored->objects[0].components = {};
  check(scenes.reload_loaded(instance, *restored).ok());
  check(!state(world).get<movement_component>(id) && lifetime.expired());
  check(scenes.reload_loaded(instance, saved).ok());
  lifetime = state(world).get<movement_component>(id)->lifetime;
  // 调用方保留明确描述；运行集合只负责销毁和创建，不承担撤销快照。
  auto saved_components = e::capture_components(world, node);
  check(bool(saved_components));
  const auto old_node = node;
  check(scenes.destroy_subtree(instance, node).ok());
  check(lifetime.expired() && !world.object(gneiss::entity_id{old_node.get()}));
  auto recreated_node = scenes.create_node(instance, {.uuid = first.uuid, .name = first.name});
  check(bool(recreated_node) && *recreated_node != old_node);
  node = *recreated_node;
  for (const auto& component : *saved_components)
    check(scenes.add_component_description(instance, node, component).ok());
  id = entity(world, node);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  lifetime = state(world).get<movement_component>(id)->lifetime;
  auto borrowed_object = borrowed_copy.find(first.uuid);
  check(borrowed_object && borrowed_object->is_valid());
  check(borrowed_copy.unload().ok());
  check(!borrowed->is_valid() && !borrowed_copy.is_valid());
  check(!borrowed_object->is_valid());
  check(!borrowed_copy.unload().ok());
  check(lifetime.expired());
  // 动态成员经借用场景创建，直接 World 删除后查询清理失效索引。
  auto dynamic_scene = scenes.create_empty(e::guid::from_bytes({20}));
  check(dynamic_scene.has_value());
  auto dynamic = e::scene_instance::borrow(world, *dynamic_scene);
  check(dynamic.has_value());
  const auto dynamic_uuid = e::guid::from_bytes({21});
  auto dynamic_root = dynamic->create({dynamic_uuid, "dynamic root"});
  check(dynamic_root.has_value());
  auto dynamic_child =
      dynamic->create({e::guid::from_bytes({22}), "dynamic child"}, {}, *dynamic_root);
  check(dynamic_child.has_value());
  auto dynamic_count = dynamic->size();
  check(dynamic_count && *dynamic_count == 2);
  check(world.entity_destroy(dynamic_root->entity()).ok());
  check(!dynamic_root->is_valid() && !dynamic_child->is_valid());
  dynamic_count = dynamic->size();
  check(dynamic_count && *dynamic_count == 0);
  check(!dynamic->find(dynamic_uuid));
  auto recreated = dynamic->create({dynamic_uuid, "recreated"});
  check(recreated && recreated->entity() != dynamic_root->entity());
  auto dynamic_members = dynamic->size();
  check(dynamic_members && *dynamic_members == 1);
  e::error cross_thread_mutation;
  std::thread dynamic_worker([&] {
    cross_thread_mutation = scenes.set_node_name(
        *dynamic_scene, gneiss::scene_node_id{recreated->entity().get()}, "wrong thread");
  });
  dynamic_worker.join();
  check(cross_thread_mutation == e::error::invalid_state);
  auto preserved_identity = recreated->identity();
  check(preserved_identity && preserved_identity->name == "recreated");
  auto detached = dynamic->create({e::guid::from_bytes({23}), "detached"}, {}, *recreated);
  check(detached && detached->reparent({}).ok());
  check(dynamic->unload().ok() && !recreated->is_valid() && !detached->is_valid());
  // Prefab 覆盖使用同一适配入口，不认识测试组件的类型。
  auto prefab = std::make_shared<e::prefab_description>();
  prefab->uuid = e::guid::from_bytes({4});
  prefab->root_uuid = first.uuid;
  prefab->objects = source.objects;
  e::game_object_description reference;
  reference.uuid = e::guid::from_bytes({5});
  reference.prefab.emplace();
  reference.prefab->uri = "asset://test.prefab";
  reference.prefab->overrides.push_back(
      {.key = {.target = {.source_node_uuid = first.uuid,
                          .target_type_guid = movement_description::type_guid,
                          .component_uuid =
                              first.components.identity(movement_description::type_guid)},
               .path = {e::meta::property_id{1}}},
       .operation = e::value{9.0F}});
  source.objects = {reference};
  e::prefab_asset_ref prefab_ref{prefab};
  check(gneiss::test_support::take_value(
            scenes.instantiate_prepared(prepare(source, prefab_ref), {}), instance)
            .ok());
  check(gneiss::test_support::take_value(scenes.find_node(instance, reference.uuid), node).ok());
  id = entity(world, node);
  check(!borrowed_copy.is_valid());
  check(state(world).get<movement_component>(id)->data.speed == 9);
  auto next = std::make_shared<e::prefab_description>(*prefab);
  next->objects[0].components.set(movement_description{3});
  check(
      scenes.reload_prefab_loaded(instance, "asset://test.prefab", e::prefab_asset_ref{next}).ok());
  check(state(world).get<movement_component>(id)->data.speed == 9);
  check(source.objects[0].components.empty());
  check(source.objects[0].prefab->overrides.size() == 1);
  check(prefab->objects[0].components.get<movement_description>()->speed == 2);
  auto empty_reload = scenes.create_empty(e::guid::generate());
  check(empty_reload.has_value());
  check(scenes.reload_prefab_loaded(*empty_reload, "asset://test.prefab", prefab_ref).ok());
  check(*scenes.get_node_count(*empty_reload) == 0U);
  lifetime = state(world).get<movement_component>(id)->lifetime;
  owner->reset();
  check(lifetime.expired());
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
