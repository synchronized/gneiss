// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/world.hpp"
#include "scene_node_info.hpp"
#include <algorithm>
namespace gneiss::test_support {
/** 测试断言辅助：通过原生句柄查询并排序，不持有场景或运行状态。 */
struct scene_access {
  engine::world& owner;
  scene_access(engine::world& value) : owner(value) {}
  template <class Host>
    requires requires(Host& host) { host.native_world(); }
  scene_access(Host& host) : owner(*host.native_world()) {}
  scene_access* operator->() { return this; }
  scene_access operator*() { return *this; }
  bool is_valid() const { return owner.is_owner_thread(); }
  engine::result<scene_instance_id> create_empty(engine::guid uuid) {
    if (!uuid)
      return engine::error::invalid_argument;
    auto created = owner.create_scene();
    if (!created)
      return created.error();
    return created->id();
  }
  engine::error unload(scene_instance_id id) {
    auto scene = engine::scene_instance::borrow(owner, id);
    return scene ? scene->unload() : scene.error();
  }
  engine::result<std::uint64_t> get_node_count(scene_instance_id id) {
    auto scene = engine::scene_instance::borrow(owner, id);
    return scene ? scene->size() : engine::result<std::uint64_t>{scene.error()};
  }
  // 来源查找只使用调用方保存的本次回执，不扫描运行对象的属性。
  engine::result<engine::game_object>
  find_object(std::span<const engine::scene_object_binding> bindings, engine::guid uuid) {
    auto found = std::ranges::find_if(
        bindings, [&](const auto& binding) { return binding.object == uuid && !binding.source; });
    return found == bindings.end() ? engine::result<engine::game_object>{engine::error::not_found}
                                   : owner.object(found->entity);
  }
  engine::result<entity_id> find_node(std::span<const engine::scene_object_binding> bindings,
                                      engine::guid uuid) {
    auto object = find_object(bindings, uuid);
    return object ? engine::result<entity_id>{entity_id{object->entity().get()}}
                  : engine::result<entity_id>{object.error()};
  }
  engine::error validate_editable_node(scene_instance_id id, entity_id node) {
    auto scene = engine::scene_instance::borrow(owner, id);
    if (!scene)
      return scene.error();
    auto object = owner.object(entity_id{node.get()});
    if (!object)
      return object.error();
    auto member = scene->contains(*object);
    if (!member)
      return member.error();
    return *member ? engine::error::success : engine::error::invalid_handle;
  }
  engine::error reparent_node(scene_instance_id id, entity_id node, entity_id parent) {
    auto status = validate_editable_node(id, node);
    if (status.failed())
      return status;
    if (parent && (status = validate_editable_node(id, parent)).failed())
      return status;
    auto object = owner.object(entity_id{node.get()});
    auto target = parent ? owner.object(entity_id{parent.get()})
                         : engine::result<engine::game_object>{engine::game_object{}};
    return object->reparent(*target);
  }
  engine::error destroy_subtree(scene_instance_id id, entity_id node) {
    auto status = validate_editable_node(id, node);
    if (status.failed())
      return status;
    return owner.object(entity_id{node.get()})->destroy();
  }
  engine::error destroy_node(scene_instance_id id, entity_id node) {
    auto status = validate_editable_node(id, node);
    if (status.failed())
      return status;
    auto scene = engine::scene_instance::borrow(owner, id);
    bool has_children = false;
    status = scene->visit_objects([&](engine::game_object object) {
      auto parent = object.parent();
      if (!parent)
        return parent.error();
      has_children |= parent->entity().get() == node.get();
      return engine::error{engine::error::success};
    });
    if (status.failed())
      return status;
    return has_children ? engine::error::invalid_state : destroy_subtree(id, node);
  }
  engine::result<test_support::scene_node_info> get_node_info(
      scene_instance_id id, std::uint64_t index,
      test_support::node_query_detail detail = test_support::node_query_detail::components) {
    auto scene = engine::scene_instance::borrow(owner, id);
    if (!scene)
      return scene.error();
    std::vector<engine::game_object> objects;
    auto status = scene->visit_objects([&](auto object) {
      objects.push_back(object);
      return engine::error{engine::error::success};
    });
    if (status.failed())
      return status;
    std::ranges::sort(objects, {}, [](auto object) { return object.entity().get(); });
    if (index >= objects.size())
      return engine::error::not_found;
    auto object = objects[index];
    auto identity = object.identity();
    auto parent = object.parent();
    if (!identity)
      return identity.error();
    if (!parent)
      return parent.error();
    test_support::scene_node_info info;
    info.node = entity_id{object.entity().get()};
    info.entity = object.entity();
    info.parent = entity_id{parent->entity().get()};
    info.uuid = identity->uuid;
    info.name = identity->name;
    if (detail != test_support::node_query_detail::identity) {
      auto components = engine::capture_components(owner, info.node);
      if (!components)
        return components.error();
      info.components = std::move(*components);
    }
    if (detail == test_support::node_query_detail::components) {
      auto transform = object.local_transform();
      if (!transform)
        return transform.error();
      info.local_transform = *transform;
    }
    return info;
  }
};
}
