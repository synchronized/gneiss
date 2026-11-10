// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/movement_component.hpp"
#include "../support/scene_access.hpp"
#include "../support/scene_application.hpp"
#include "../support/scene_instantiation.hpp"
#include "../support/value_result.hpp"
#include "engine/core/meta/object_type.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/property_edit_policy.hpp"
#include "engine/core/serialization/json.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/component_identity.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/composition.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/resource/asset_file.hpp"
#include "engine/resource/scene.hpp"
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
struct runtime_only_description
    : e::meta::object_type<runtime_only_description, e::component_description_base> {
  inline static constexpr e::guid type_guid = e::guid::from_bytes({84, 78});
};
struct runtime_marker {
  bool active = true;
};
struct resolved_link {
  e::game_object target;
};
const e::guid link_target_uuid = e::guid::from_bytes({84, 77});
e::scene_content prepare(e::scene_description scene, e::prefab_asset_ref prefab = {}) {
  e::scene_content output;
  output.description = e::make_ref<const e::scene_description>(std::move(scene));
  if (prefab)
    output.prefabs.emplace(
        "asset://test.prefab",
        e::scene_prefab_dependency{.resource = {}, .description = std::move(prefab)});
  auto composed = gneiss::test_support::compose(output);
  check(bool(composed));
  check(e::scene_dependencies(*composed)->empty());
  return output;
}
}
int main() try {
  check(e::initialize_meta(register_movement_meta).ok());
  check(e::register_scene_component(e::component_adapter{}) == e::error::invalid_argument);
  auto runtime_operations = operations();
  runtime_operations.properties.reset();
  const auto runtime_registration = e::make_component_adapter(runtime_operations);
  check(bool(runtime_registration.install) && bool(runtime_registration.capture));
  check(!runtime_registration.properties.read && !runtime_registration.properties.edit &&
        !runtime_registration.properties.modify);
  const auto editable_registration = e::make_component_adapter(operations());
  check(bool(editable_registration.properties.read) &&
        bool(editable_registration.properties.edit) &&
        bool(editable_registration.properties.modify));
  e::component_adapter runtime_only;
  runtime_only.type = runtime_only_description::type_guid;
  runtime_only.validate = [](const e::component_description& value) -> e::error {
    return value && value->get<runtime_only_description>() ? e::error::success
                                                           : e::error::invalid_argument;
  };
  runtime_only.install = [](e::component_context& context,
                            const e::component_description&) -> e::error {
    state(*context.target.owner())
        .emplace_or_replace<runtime_marker>(context.target.entity().get());
    return e::error::success;
  };
  runtime_only.capture =
      [](e::game_object target) -> e::result<std::optional<e::component_description>> {
    if (!state(*target.owner()).get<runtime_marker>(target.entity().get()))
      return std::optional<e::component_description>{};
    return std::optional<e::component_description>{
        e::make_component_description(runtime_only_description{})};
  };
  runtime_only.remove = [](e::game_object target) {
    return state(*target.owner()).remove<runtime_marker>(target.entity().get());
  };
  runtime_only.equal = [](const e::component_description&, const e::component_description&) {
    return true;
  };
  check(e::register_scene_component(runtime_only).ok());
  check(e::register_scene_component(runtime_only) == e::error::invalid_argument);
  auto custom = operations();
  const auto alias = e::guid::from_bytes({84, 99});
  custom.properties->property_type = alias;
  custom.resolve_references = [](e::component_context& context,
                                 const movement_description& desc) -> e::error {
    if (desc.speed != 17)
      return e::error::success;
    if (!context.lookup_object)
      return e::error::not_ready;
    auto target = context.lookup_object({link_target_uuid, {}});
    if (!target)
      return target.error();
    // 解析时目标组件必须已经安装，而不仅仅是对象已经存在。
    if (!state(*context.target.owner()).get<movement_component>(target->entity().get()))
      return e::error::not_ready;
    state(*context.target.owner())
        .emplace_or_replace<resolved_link>(context.target.entity().get(), *target);
    return e::error::success;
  };
  check(e::register_scene_component(custom).ok());
  check(e::register_scene_component(operations()) == e::error::invalid_argument);
  check(e::component_description_type(alias) == movement_description::type_guid);
  check(e::component_description_type(movement_description::type_guid) ==
        movement_description::type_guid);
  check(e::component_description_type(e::camera_description::type_guid) ==
        e::camera_description::type_guid);
  check(e::component_description_type(unknown_description::type_guid) ==
        unknown_description::type_guid);
  const auto primary =
      e::meta::find_property(e::camera_description::type_guid, e::meta::property_id{4});
  check(primary && gneiss::engine::is_edit_read_only(primary));
  check(gneiss::engine::meta::has_flags(primary.capabilities(),
                                        gneiss::engine::meta::property_capabilities::writable));
  e::camera_description camera{{1, 0.1F, 100}, true};
  auto saved_camera = gneiss::engine::serialization::json::encode_bytes(camera);
  check(saved_camera.has_value());
  auto loaded_camera =
      gneiss::engine::serialization::json::decode<e::camera_description>(*saved_camera);
  check(loaded_camera && loaded_camera->is_primary);
  // 纯场景和组件操作不需要文件服务、渲染服务或资产管理器。
  auto owner = e::create_native();
  check(owner.has_value());
  auto& world = **owner;
  gneiss::test_support::scene_access scenes{world};
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
  second.parent.object = first.uuid;
  source.objects.push_back(second);
  // 真实文件格式解码后进入统一准备与同步实例化路径。
  auto encoded = gneiss::engine::asset_file::serialize_bytes(source);
  check(encoded.has_value());
  auto decoded = gneiss::engine::asset_file::deserialize<e::scene_description>(*encoded);
  check(decoded.has_value());
  auto prepared = prepare(*decoded);
  check(e::register_scene_component(operations()) == e::error::invalid_state);
  auto created =
      gneiss::test_support::scene_fixture::instantiate_scene(world, std::move(prepared), {});
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
  auto another_scene =
      gneiss::test_support::scene_fixture::instantiate_scene(world, prepare(source), {});
  check(another_scene.has_value());
  auto another_handle = e::scene_instance::borrow(world, another_scene->instance);
  check(another_handle.has_value());
  auto first_object = scenes.find_object(created->objects, first.uuid);
  auto foreign_parent = scenes.find_object(another_scene->objects, first.uuid);
  check(first_object && foreign_parent);
  check(first_object->reparent(*foreign_parent) == e::error::invalid_argument);
  check(world.find(gneiss::entity_id{(created->objects[0].node).get()})
            .reparent(world.find(gneiss::entity_id{(another_scene->objects[0].node).get()})) ==
        e::error::invalid_argument);
  auto unchanged_parent = first_object->parent();
  check(unchanged_parent && !unchanged_parent->is_valid());
  // 不能通过未登记的中间对象绕开不同场景的成员边界。
  auto bridge = world.create_object({e::guid::from_bytes({9}), "bridge"});
  check(bridge.has_value());
  check(first_object->reparent(*bridge) == e::error::invalid_argument);
  check(bridge->reparent(*foreign_parent) == e::error::invalid_argument);
  check(first_object->reparent({}).ok());
  check(bridge->destroy().ok());
  auto parent_bridge =
      world.create_object({e::guid::from_bytes({10}), "parent bridge"}, {}, *foreign_parent);
  check(parent_bridge.has_value());
  auto inherited_count = another_handle->size();
  check(inherited_count && *inherited_count == 3);
  check(first_object->reparent(*parent_bridge) == e::error::invalid_argument);
  check(another_handle->unload().ok());
  check(!parent_bridge->is_valid());
  check(first_object->is_valid());
  auto other_world = e::create_native();
  check(other_world.has_value());
  check(!e::scene_instance::borrow(**other_world, instance));
  e::error wrong_thread;
  std::thread scene_worker([&] { wrong_thread = borrowed_copy.unload(); });
  scene_worker.join();
  check(wrong_thread == e::error::invalid_state);
  check(borrowed_copy.is_valid());
  auto found = scenes.find_node(created->objects, first.uuid);
  check(found.has_value());
  auto node = *found;
  auto queried = scenes.get_node_info(instance, 0);
  check(queried && queried->components.get<movement_description>() &&
        queried->components.get<movement_description>()->speed == 2);
  auto invalid = scenes.get_node_count({});
  check(!invalid && invalid.error() == e::error::invalid_handle);
  auto missing = scenes.find_node(created->objects, {});
  check(!missing && missing.error() == e::error::not_found);
  auto captured = e::capture_components(world, node);
  check(bool(captured));
  check(captured->identity(movement_description::type_guid) ==
        first.components.identity(movement_description::type_guid));
  auto id = entity(world, node);
  auto* component = state(world).get<movement_component>(id);
  check(component && component->data.speed == 2);
  component->data.speed = 4;
  check(e::edit_runtime_component({*world.object(gneiss::entity_id{node.get()})},
                                  movement_description::type_guid, e::meta::property_id{1},
                                  e::value{5.0F})
            .ok());
  check(state(world).get<movement_component>(id)->data.speed == 5);
  check(e::edit_runtime_component({*world.object(gneiss::entity_id{node.get()})},
                                  movement_description::type_guid, e::meta::property_id{1},
                                  e::value{-1.0F}) == e::error::invalid_argument);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  // 文件往返使用调用方描述，显式采集需要保留的组件，不捕获整个运行场景。
  e::scene_description saved = source;
  for (auto& object : saved.objects) {
    auto member = scenes.find_node(created->objects, object.uuid);
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
  auto baseline = e::compose_scene(source, {});
  check(bool(baseline));
  auto mapping = associations(created->objects);
  const auto apply = [&](const e::scene_description& description) {
    auto next = e::compose_scene(description, {});
    if (!next)
      return next.error();
    auto result = e::apply_scene(world, instance, mapping, *baseline, *next, {});
    if (!result)
      return result.error();
    if (!*result)
      return e::error{e::error::invalid_state};
    baseline = std::move(next);
    return e::error{e::error::success};
  };
  state(world).get<movement_component>(id)->data.speed = 8;
  check(apply(*restored).ok());
  check(state(world).get<movement_component>(id)->data.speed == 5);
  // 单组件失败不得破坏已有组件；跨对象应用不再承诺整体回滚。
  check(e::update_component({*world.object(gneiss::entity_id{node.get()})},
                            e::make_ref<movement_description>(13)) == e::error::invalid_state);
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
  check(apply(*restored).ok());
  check(!state(world).get<movement_component>(id) && lifetime.expired());
  check(apply(saved).ok());
  lifetime = state(world).get<movement_component>(id)->lifetime;
  // 调用方保留明确描述；运行集合只负责销毁和创建，不承担撤销快照。
  auto saved_components = e::capture_components(world, node);
  check(bool(saved_components));
  const auto old_node = node;
  check(scenes.destroy_subtree(instance, node).ok());
  check(lifetime.expired() && !world.object(gneiss::entity_id{old_node.get()}));
  auto recreated_node = borrowed->create({first.uuid, first.name});
  check(bool(recreated_node) && recreated_node->entity() != old_node);
  node = recreated_node->entity();
  for (const auto& component : *saved_components)
    check(e::add_component({*world.object(gneiss::entity_id{node.get()})}, component).ok());
  id = entity(world, node);
  check(state(world).get<movement_component>(id)->data.speed == 5);
  lifetime = state(world).get<movement_component>(id)->lifetime;
  auto borrowed_object = world.object(gneiss::entity_id{node.get()});
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
  check(!dynamic_root->is_valid());
  auto recreated = dynamic->create({dynamic_uuid, "recreated"});
  check(recreated && recreated->entity() != dynamic_root->entity());
  const auto recreated_entity = recreated->entity();
  check(recreated->set_name("renamed").ok());
  auto renamed = recreated->identity();
  check(renamed && renamed->uuid == dynamic_uuid && renamed->name == "renamed");
  auto transform = recreated->local_transform();
  check(transform.has_value());
  transform->translation.x = 7.0F;
  check(recreated->set_local_transform(*transform).ok());
  check(recreated->entity() == recreated_entity);
  auto moved = recreated->world_transform();
  check(moved && moved->translation.x == 7.0F);
  check(recreated->set_name("recreated").ok());
  auto dynamic_members = dynamic->size();
  check(dynamic_members && *dynamic_members == 1);
  e::error cross_thread_mutation;
  std::thread dynamic_worker([&] { cross_thread_mutation = recreated->set_name("wrong thread"); });
  dynamic_worker.join();
  check(cross_thread_mutation == e::error::invalid_state);
  auto preserved_identity = recreated->identity();
  check(preserved_identity && preserved_identity->name == "recreated");
  auto detached = dynamic->create({e::guid::from_bytes({23}), "detached"}, {}, *recreated);
  check(detached && detached->reparent({}).ok());
  check(dynamic->unload().ok() && !recreated->is_valid() && !detached->is_valid());
  check(recreated->set_name("deleted") == e::error::invalid_handle);
  check(recreated->set_local_transform({}) == e::error::invalid_handle);
  check(!recreated->local_transform());
  check(e::remove_component({*recreated}, movement_description::type_guid) ==
        e::error::invalid_handle);
  check(e::add_component({}, e::make_ref<movement_description>(1)) == e::error::invalid_handle);
  check(e::resolve_component_references({}, {}) == e::error::invalid_handle);

  check(e::game_object{}.set_name("empty") == e::error::invalid_handle);

  // 运行注册无需编辑属性回调；编辑不可删除不影响运行移除。
  auto runtime_node = world.create_object({e::guid::generate(), "runtime-only"});
  check(runtime_node.has_value());
  auto runtime_object = runtime_node;
  check(runtime_object.has_value());
  auto runtime_description = e::make_component_description(runtime_only_description{});
  check(e::component_dependencies({runtime_description})->empty());
  check(e::add_component({*runtime_object}, runtime_description).ok());
  check(e::read_runtime_component(world, gneiss::entity_id{runtime_node->entity().get()},
                                  runtime_only_description::type_guid, e::meta::property_id{1})
            .error() == e::error::unsupported);
  check(e::edit_runtime_component({*runtime_object}, runtime_only_description::type_guid,
                                  e::meta::property_id{1},
                                  e::value{1.0F}) == e::error::unsupported);
  check(e::modify_component(runtime_description, [](e::meta::instance) {
          return e::error::success;
        }).error() == e::error::unsupported);
  e::component_descriptions runtime_values;
  runtime_values.set(runtime_description);
  check(e::edit_component(runtime_values, runtime_only_description::type_guid,
                          e::meta::property_id{1}, e::value{1.0F}) == e::error::unsupported);
  check(e::remove_component({*runtime_object}, runtime_only_description::type_guid).ok());
  check(e::remove_component({*runtime_object}, runtime_only_description::type_guid) ==
        e::error::not_found);
  check(runtime_object->destroy().ok());
  // 装配只安装默认提示；显式激活后，其他装配成功或失败均不能改变活动相机。
  e::scene_description camera_scene;
  camera_scene.uuid = e::guid::generate();
  e::game_object_description camera_object;
  camera_object.uuid = e::guid::generate();
  camera_object.components.set(e::camera_description{{1, 0.1F, 100}, true});
  camera_scene.objects = {camera_object};
  auto camera_instance =
      gneiss::test_support::scene_fixture::instantiate_scene(world, prepare(camera_scene), {});
  check(bool(camera_instance));
  gneiss::entity_id active_camera;
  check(world.get_active_camera(active_camera) == e::error::not_ready);
  active_camera = camera_instance->objects.front().entity;
  check(world.set_active_camera(active_camera).ok());
  auto broken_scene = camera_scene;
  broken_scene.uuid = e::guid::generate();
  broken_scene.objects.front().uuid = e::guid::generate();
  e::game_object_description failing_object;
  failing_object.uuid = e::guid::generate();
  failing_object.components.set(movement_description{13});
  broken_scene.objects.push_back(failing_object);
  const auto entities_before_failure = *world.entity_count();
  auto failed_creation =
      gneiss::test_support::scene_fixture::instantiate_scene(world, prepare(broken_scene), {});
  check(!failed_creation && failed_creation.error() == e::error::invalid_state);
  gneiss::entity_id camera_after_failure;
  check(world.get_active_camera(camera_after_failure).ok() &&
        camera_after_failure == active_camera);
  check(*world.entity_count() == entities_before_failure);
  // 同一组合结果重复追加到非空场景，保留旧对象并提供各自的来源回执。
  auto destination = e::scene_instance::borrow(world, camera_instance->instance);
  check(destination.has_value());
  auto existing_parent = scenes.find_object(camera_instance->objects, camera_object.uuid);
  check(existing_parent.has_value());
  e::scene_description append_source;
  append_source.uuid = e::guid::generate();
  e::game_object_description append_root;
  append_root.uuid = e::guid::generate();
  e::game_object_description append_child;
  append_child.uuid = e::guid::generate();
  append_child.parent.object = append_root.uuid;
  append_source.objects = {append_child, append_root};
  auto append_composed = e::compose_scene(append_source, {});
  check(append_composed.has_value());
  auto appended = e::instantiate(*destination, *append_composed, {}, *existing_parent);
  auto appended_again = e::instantiate(*destination, *append_composed, {}, *existing_parent);
  check(appended && appended_again && *destination->size() == 5);
  check(appended->roots.size() == 1 && appended_again->roots.size() == 1);
  check(appended->roots[0] != appended_again->roots[0]);
  check(*appended->roots[0].parent() == *existing_parent);
  auto child_object = world.object(appended->objects.front().entity);
  check(child_object && *child_object->parent() == appended->roots[0]);
  const auto before_append_failure = *world.entity_count();
  auto broken_composed = e::compose_scene(broken_scene, {});
  check(broken_composed.has_value());
  auto failed_append = e::instantiate(*destination, *broken_composed, {}, *existing_parent);
  check(!failed_append && failed_append.error() == e::error::invalid_state);
  check(*world.entity_count() == before_append_failure && *destination->size() == 5);
  check(existing_parent->is_valid() && appended->roots[0].is_valid());
  check(world.get_active_camera(camera_after_failure).ok() &&
        camera_after_failure == active_camera);
  auto linked = append_source;
  linked.objects.front().components.set(movement_description{17});
  linked.objects.back().uuid = link_target_uuid;
  linked.objects.front().parent.object = link_target_uuid;
  linked.objects.back().components.set(movement_description{18});
  auto link_composed = e::compose_scene(linked, {});
  check(link_composed.has_value());
  auto link_one = e::instantiate(*destination, *link_composed, {});
  auto link_two = e::instantiate(*destination, *link_composed, {});
  check(link_one && link_two);
  const auto* link_a = state(world).get<resolved_link>(link_one->objects.front().entity.get());
  const auto* link_b = state(world).get<resolved_link>(link_two->objects.front().entity.get());
  check(link_a && link_b && link_a->target == link_one->roots.front());
  check(link_b->target == link_two->roots.front() && link_b->target != link_a->target);
  auto missing_link = *link_composed;
  missing_link.objects.pop_back();
  missing_link.objects.front().parent = {};
  const auto before_missing_link = *world.entity_count();
  auto failed_link = e::instantiate(*destination, missing_link, {});
  check(!failed_link && failed_link.error() == e::error::not_found);
  check(*world.entity_count() == before_missing_link);
  check(link_one->roots.front().destroy().ok() && link_two->roots.front().destroy().ok());
  auto malformed = *append_composed;
  malformed.objects.back().parent = malformed.objects.front().address;
  check(!e::instantiate(*destination, malformed, {}));
  check(*world.entity_count() == before_append_failure);
  check(!e::instantiate(e::scene_instance{}, *append_composed, {}));
  auto foreign_scene_id = scenes.create_empty(e::guid::generate());
  check(foreign_scene_id.has_value());
  auto foreign_scene = e::scene_instance::borrow(world, *foreign_scene_id);
  check(foreign_scene.has_value());
  auto append_foreign_parent = foreign_scene->create({e::guid::generate(), "foreign"});
  check(append_foreign_parent.has_value());
  check(!e::instantiate(*destination, *append_composed, {}, *append_foreign_parent));
  check(foreign_scene->unload().ok());
  check(scenes.unload(camera_instance->instance).ok());
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
  auto prefab_created = gneiss::test_support::scene_fixture::instantiate_scene(
      world, prepare(source, prefab_ref), {});
  check(bool(prefab_created));
  instance = prefab_created->instance;
  auto root_binding =
      std::ranges::find(prefab_created->objects, first.uuid, &e::scene_object_binding::source);
  check(root_binding != prefab_created->objects.end());
  node = root_binding->node;
  id = entity(world, node);
  check(!borrowed_copy.is_valid());
  check(state(world).get<movement_component>(id)->data.speed == 9);
  auto next = std::make_shared<e::prefab_description>(*prefab);
  next->objects[0].components.set(movement_description{3});
  auto old_composition = e::compose_scene(source, {{"asset://test.prefab", prefab.get()}});
  auto next_composition = e::compose_scene(source, {{"asset://test.prefab", next.get()}});
  check(bool(old_composition) && bool(next_composition));
  auto prefab_mapping = associations(prefab_created->objects);
  auto applied =
      e::apply_scene(world, instance, prefab_mapping, *old_composition, *next_composition, {});
  check(applied && *applied);
  check(state(world).get<movement_component>(id)->data.speed == 9);
  check(source.objects[0].components.empty());
  check(source.objects[0].prefab->overrides.size() == 1);
  check(prefab->objects[0].components.get<movement_description>()->speed == 2);
  auto empty_reload = scenes.create_empty(e::guid::generate());
  check(empty_reload.has_value());
  std::map<e::object_address, e::scene_object_association> empty_mapping;
  auto empty_applied = e::apply_scene(world, *empty_reload, empty_mapping, {}, {}, {});
  check(empty_applied && *empty_applied);
  check(*scenes.get_node_count(*empty_reload) == 0U);
  // 替换保留场景句柄，但旧实体释放，归属不能停留在临时实例 ID 上。
  e::scene_description replacement_source;
  replacement_source.uuid = e::guid::generate();
  replacement_source.objects = {first};
  auto replacing = gneiss::test_support::scene_fixture::instantiate_scene(
      world, prepare(replacement_source), {});
  auto independent = gneiss::test_support::scene_fixture::instantiate_scene(
      world, prepare(replacement_source), {});
  check(replacing && independent);
  auto stable = e::scene_instance::borrow(world, replacing->instance);
  auto other = e::scene_instance::borrow(world, independent->instance);
  check(stable && other);
  auto old_object = scenes.find_object(replacing->objects, first.uuid);
  auto other_object = scenes.find_object(independent->objects, first.uuid);
  check(old_object && other_object);
  replacement_source.objects.front().name = "replacement";
  auto replaced = gneiss::test_support::scene_fixture::instantiate_scene(
      world, prepare(replacement_source), {});
  check(replaced.has_value());
  check(scenes.unload(replacing->instance).ok());
  check(!stable->is_valid());
  check(replaced->instance != replacing->instance);
  stable = e::scene_instance::borrow(world, replaced->instance);
  check(stable && stable->is_valid());
  check(!old_object->is_valid() && other_object->is_valid());
  auto new_object = scenes.find_object(replaced->objects, first.uuid);
  check(new_object && new_object->identity()->name == "replacement");
  check(*stable->size() == 1 && *other->size() == 1);
  check(stable->unload().ok());
  check(!new_object->is_valid() && other_object->is_valid());
  check(other->unload().ok() && !other_object->is_valid());
  lifetime = state(world).get<movement_component>(id)->lifetime;
  owner->reset();
  check(lifetime.expired());
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
