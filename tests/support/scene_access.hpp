// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include "scene_node_info.hpp"
#include <algorithm>
namespace gneiss::test_support {
/** 测试断言辅助：通过原生句柄查询并排序，不持有场景或运行状态。 */
struct scene_access {
  gneiss::world& owner;
  scene_access(gneiss::world& value) : owner(value) {}
  template <class Host>
    requires requires(Host& host) { host.native_world(); }
  scene_access(Host& host) : owner(*host.native_world()) {}
  scene_access* operator->() { return this; }
  scene_access operator*() { return *this; }
  bool is_valid() const { return owner.is_owner_thread(); }
  gneiss::result<scene_instance> create_empty(gneiss::guid uuid) {
    if (!uuid)
      return gneiss::error::invalid_argument;
    auto created = owner.create_scene();
    if (!created)
      return created.error();
    return *created;
  }
  gneiss::error unload(scene_instance id) {
    if (id.owner() != &owner)
      return gneiss::error::invalid_handle;
    auto scene = id;
    return scene.unload();
  }
  gneiss::result<std::uint64_t> get_node_count(scene_instance id) {
    if (id.owner() != &owner)
      return gneiss::error::invalid_handle;
    auto scene = id;
    return scene.size();
  }
  // 来源查找只使用调用方保存的本次回执，不扫描运行对象的属性。
  gneiss::result<gneiss::game_object>
  find_object(std::span<const gneiss::scene_object_binding> bindings, gneiss::guid uuid) {
    auto found = std::ranges::find_if(
        bindings, [&](const auto& binding) { return binding.object == uuid && !binding.source; });
    return found == bindings.end() ? gneiss::result<gneiss::game_object>{gneiss::error::not_found}
                                   : owner.object(found->entity);
  }
  gneiss::result<entity_id> find_node(std::span<const gneiss::scene_object_binding> bindings,
                                      gneiss::guid uuid) {
    auto object = find_object(bindings, uuid);
    return object ? gneiss::result<entity_id>{entity_id{object->entity().get()}}
                  : gneiss::result<entity_id>{object.error()};
  }
  gneiss::error validate_editable_node(scene_instance id, entity_id node) {
    if (id.owner() != &owner)
      return gneiss::error::invalid_handle;
    auto scene = id;
    auto object = owner.object(entity_id{node.get()});
    if (!object)
      return object.error();
    auto member = scene.contains(*object);
    if (!member)
      return member.error();
    return *member ? gneiss::error::success : gneiss::error::invalid_handle;
  }
  gneiss::error reparent_node(scene_instance id, entity_id node, entity_id parent) {
    auto status = validate_editable_node(id, node);
    if (status.failed())
      return status;
    if (parent && (status = validate_editable_node(id, parent)).failed())
      return status;
    auto object = owner.object(entity_id{node.get()});
    auto target = parent ? owner.object(entity_id{parent.get()})
                         : gneiss::result<gneiss::game_object>{gneiss::game_object{}};
    return object->reparent(*target);
  }
  gneiss::error destroy_subtree(scene_instance id, entity_id node) {
    auto status = validate_editable_node(id, node);
    if (status.failed())
      return status;
    return owner.object(entity_id{node.get()})->destroy();
  }
  gneiss::error destroy_node(scene_instance id, entity_id node) {
    auto status = validate_editable_node(id, node);
    if (status.failed())
      return status;
    if (id.owner() != &owner)
      return gneiss::error::invalid_handle;
    auto scene = id;
    bool has_children = false;
    status = scene.visit_objects([&](gneiss::game_object object) {
      auto parent = object.parent();
      if (!parent)
        return parent.error();
      has_children |= parent->entity().get() == node.get();
      return gneiss::error{gneiss::error::success};
    });
    if (status.failed())
      return status;
    return has_children ? gneiss::error::invalid_state : destroy_subtree(id, node);
  }
  gneiss::result<test_support::scene_node_info> get_node_info(
      scene_instance id, std::uint64_t index,
      test_support::node_query_detail detail = test_support::node_query_detail::components) {
    if (id.owner() != &owner)
      return gneiss::error::invalid_handle;
    auto scene = id;
    std::vector<gneiss::game_object> objects;
    auto status = scene.visit_objects([&](auto object) {
      objects.push_back(object);
      return gneiss::error{gneiss::error::success};
    });
    if (status.failed())
      return status;
    std::ranges::sort(objects, {}, [](auto object) { return object.entity().get(); });
    if (index >= objects.size())
      return gneiss::error::not_found;
    auto object = objects[index];
    const std::string name(object.name());
    auto parent = object.parent();
    if (!parent)
      return parent.error();
    test_support::scene_node_info info;
    info.node = entity_id{object.entity().get()};
    info.entity = object.entity();
    info.parent = entity_id{parent->entity().get()};
    info.name = name;
    if (detail != test_support::node_query_detail::identity) {
      auto components = gneiss::capture_components(owner, info.node);
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
