// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/scene/scene_application.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
#include <algorithm>
#include <set>
namespace gneiss::engine {
namespace {
bool same_transform(const math::transform& a, const math::transform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
} // namespace
result<bool> apply_scene(world& world, scene_instances& scenes, scene_instance_id scene,
                         std::map<object_address, scene_object_association>& associations,
                         const composed_scene& previous, const composed_scene& next,
                         const scene_resource_bindings& assets,
                         const scene_component_preparer& prepare) {
  if (!world.is_owner_thread())
    return error::invalid_state;
  if (world.scenes() != &scenes)
    return error::invalid_argument;
  std::map<object_address, const composed_object*> old;
  for (const auto& object : previous.objects)
    if (!old.emplace(object.address, &object).second)
      return error::invalid_argument;
  std::map<object_address, const composed_object*> desired;
  for (const auto& object : next.objects)
    if (!object.address.object || !desired.emplace(object.address, &object).second)
      return error::invalid_argument;
  for (const auto& object : next.objects)
    if (object.parent.object && !desired.contains(object.parent))
      return error::invalid_state;
  // 在任何运行修改前检查目标层级，拒绝循环和自引用。
  for (const auto& object : next.objects) {
    std::size_t depth = 0;
    auto parent = object.parent;
    while (parent.object) {
      if (++depth > desired.size())
        return error::invalid_argument;
      parent = desired.at(parent)->parent;
    }
  }
  struct projected_object {
    scene_node_id node;
    math::transform local_transform;
  };
  std::map<object_address, projected_object> nodes;
  auto count = scenes.get_node_count(scene);
  if (!count)
    return count.error();
  std::set<std::uint64_t> entities;
  for (const auto& [address, binding] : associations) {
    if (binding.node.get() != binding.entity.get() || !entities.insert(binding.entity.get()).second)
      return error::invalid_argument;
    if (scenes.validate_editable_node(scene, binding.node).failed())
      return false;
    auto borrowed = world.object(binding.entity);
    if (!borrowed)
      return false;
    auto transform = borrowed->local_transform();
    if (!transform)
      return transform.error();
    projected_object info;
    info.node = binding.node;
    info.local_transform = *transform;
    nodes.emplace(address, std::move(info));
  }
  if (nodes.size() != *count)
    return false;
  // 所有目标及基线先检查，再开始更新；不把外部变更当作已同步状态。
  if (nodes.size() != old.size())
    return false;
  for (const auto& object : previous.objects) {
    auto found = nodes.find(object.address);
    if (found == nodes.end() ||
        !same_transform(found->second.local_transform, old.at(object.address)->effective.transform))
      return false;
  }
  struct component_update {
    object_address address;
    guid type;
    component_description value;
    bool remove{}, add{};
  };
  std::vector<component_update> updates;
  for (const auto& object : next.objects) {
    const component_descriptions empty;
    auto previous_object = old.find(object.address);
    const auto& previous_components =
        previous_object == old.end() ? empty : previous_object->second->effective.components;
    const auto& components = object.effective.components;
    const auto node =
        previous_object == old.end() ? scene_node_id{} : nodes.at(object.address).node;
    for (const auto& previous_component : previous_components) {
      const auto* replacement = components.find(previous_component->guid());
      if (!replacement || (*replacement)->uuid != previous_component->uuid)
        updates.push_back({object.address, previous_component->guid(), {}, true, false});
    }
    for (const auto& component : components) {
      const auto* previous_component = previous_components.find(component->guid());
      const bool add = !previous_component || (*previous_component)->uuid != component->uuid;
      if (!add && equal_components({*previous_component}, {component}))
        continue;
      auto value = add || !prepare ? result<component_description>{component}
                                   : prepare(object, node, component);
      if (!value)
        return value.error();
      updates.push_back({object.address, component->guid(), std::move(*value), false, add});
    }
  }
  // 先拆开所有变化的父关系，避免合法的父子反转被旧树误判为循环。
  for (const auto& object : next.objects) {
    if (!old.contains(object.address) || old.at(object.address)->parent == object.parent)
      continue;
    auto status = world.node_reparent(nodes.at(object.address).node, {});
    if (status.failed())
      return status;
  }
  // 只销毁消失对象，从叶到根处理；其余实体保持原身份。
  std::vector<std::pair<std::size_t, object_address>> removed;
  for (const auto& object : previous.objects) {
    if (desired.contains(object.address))
      continue;
    std::size_t depth = 0;
    auto parent = object.parent;
    while (parent.object) {
      auto found = old.find(parent);
      if (found == old.end() || ++depth > old.size())
        return error::invalid_state;
      parent = found->second->parent;
    }
    removed.emplace_back(depth, object.address);
  }
  std::ranges::sort(removed, std::greater<>{});
  for (const auto& [depth, address] : removed) {
    auto status = world.object_destroy(nodes.at(address).node);
    if (status.failed())
      return status;
    nodes.erase(address);
    associations.erase(address);
  }
  // 新对象先脱离层级创建，随后统一连接，描述排列不必父节点在前。
  for (const auto& object : next.objects) {
    if (old.contains(object.address))
      continue;
    auto created = scenes.create_node(
        scene, {.uuid = object.address.source ? guid::generate() : object.address.object,
                .name = object.effective.name,
                .local_transform = object.effective.transform});
    if (!created)
      return created.error();
    projected_object info;
    info.node = *created;
    associations[object.address] = {*created, entity_id{created->get()}};
    nodes.emplace(object.address, std::move(info));
  }
  for (const auto& object : next.objects) {
    auto previous_object = old.find(object.address);
    const auto node = nodes.at(object.address).node;
    if ((previous_object == old.end() || previous_object->second->parent != object.parent) &&
        object.parent.object) {
      auto status = world.node_reparent(node, nodes.at(object.parent).node);
      if (status.failed())
        return status;
    }
    if (previous_object != old.end() &&
        previous_object->second->effective.name != object.effective.name) {
      auto identity = world.node_identity(node);
      if (!identity)
        return identity.error();
      auto status = world.node_set_identity(node, {identity->uuid, object.effective.name});
      if (status.failed())
        return status;
    }
  }
  const auto prepared_binding = [&](const composed_object& object) {
    scene_mesh_binding binding;
    if (const auto* renderer = object.effective.components.get<mesh_renderer_description>()) {
      if (auto found = assets.find(renderer->mesh_uri); found != assets.end())
        binding.mesh = resource_cast<mesh>(found->second);
      if (auto found = assets.find(renderer->material_uri); found != assets.end())
        binding.material = resource_cast<material>(found->second);
    }
    return binding;
  };
  for (const auto& update : updates) {
    const auto node = nodes.at(update.address).node;
    const auto prepared = prepared_binding(*desired.at(update.address));
    component_context context{world, node, {prepared.mesh, prepared.material}};
    auto status = update.remove ? remove_component(context, update.type)
                  : update.add  ? add_component(context, update.value, update.value->uuid)
                                : update_component(context, update.value);
    if (status.failed())
      return status;
  }
  // 使用调用方提供的资源身份，不用当前缓存替换同 URI 引用。
  for (const auto& object : next.objects) {
    const auto* renderer = object.effective.components.get<mesh_renderer_description>();
    if (!renderer)
      continue;
    const auto target_binding = prepared_binding(object);
    const auto node = nodes.at(object.address).node;
    auto current = capture_mesh_binding(world, scene, node);
    if (!current)
      return current.error();
    if (current->mesh == target_binding.mesh && current->material == target_binding.material)
      continue;
    auto status = replace_mesh_binding(
        world, scene, node, *current, target_binding,
        object.effective.components.identity(mesh_renderer_description::type_guid));
    if (status.failed())
      return status;
  }
  for (const auto& object : next.objects) {
    if (!old.contains(object.address) ||
        same_transform(old.at(object.address)->effective.transform, object.effective.transform))
      continue;
    auto target = world.object(entity_id{nodes.at(object.address).node.get()});
    if (!target)
      return target.error();
    auto status = target->set_local_transform(object.effective.transform);
    if (status.failed())
      return status;
  }
  return true;
}
} // namespace gneiss::engine
