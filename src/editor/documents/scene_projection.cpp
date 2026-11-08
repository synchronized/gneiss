// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_projection.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include <algorithm>
#include <set>
namespace gneiss::editor {
engine::error
scene_projection::reset(std::span<const engine::scene_object_binding> objects) noexcept try {
  std::map<engine::object_address, object_binding> next;
  std::set<std::uint64_t> entities;
  for (const auto& object : objects) {
    if (!object.object || !object.node || object.entity.get() != object.node.get() ||
        !entities.insert(object.entity.get()).second ||
        !next.emplace(engine::object_address{object.object, object.source},
                      object_binding{object.node, object.entity})
             .second)
      return engine::error::invalid_argument;
  }
  objects_.swap(next);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
namespace {
bool same_transform(const engine::math::transform& a, const engine::math::transform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
engine::result<bool>
update_objects(engine::world& world, engine::scene_instances& scenes, scene_instance_id scene,
               std::map<engine::object_address, scene_projection::object_binding>& associations,
               const scene_document_snapshot& before, const scene_document_snapshot& target,
               bool prepare_editor) {
  if (before.content.get().uuid != target.content.get().uuid)
    return false;
  auto previous = compose_scene_document(before);
  auto next = compose_scene_document(target);
  if (!previous)
    return previous.error();
  if (!next)
    return next.error();
  std::map<engine::object_address, const engine::composed_object*> old;
  for (const auto& object : previous->objects)
    old.emplace(object.address, &object);
  std::map<engine::object_address, const engine::composed_object*> desired;
  for (const auto& object : next->objects)
    desired.emplace(object.address, &object);
  for (const auto& object : next->objects)
    if (object.parent.object && !desired.contains(object.parent))
      return engine::error::invalid_state;
  struct projected_object {
    scene_node_id node;
    engine::math::transform local_transform;
  };
  std::map<engine::object_address, projected_object> nodes;
  auto count = scenes.get_node_count(scene);
  if (!count)
    return count.error();
  for (const auto& [address, binding] : associations) {
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
  for (const auto& object : previous->objects) {
    auto found = nodes.find(object.address);
    if (found == nodes.end() ||
        !same_transform(found->second.local_transform, old.at(object.address)->effective.transform))
      return false;
  }
  struct component_update {
    engine::object_address address;
    engine::guid type;
    engine::component_description value;
    bool remove{}, add{};
  };
  std::vector<component_update> updates;
  for (const auto& object : next->objects) {
    const engine::component_descriptions empty;
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
      if (!add && engine::equal_components({*previous_component}, {component}))
        continue;
      auto value = (add || (!prepare_editor && !object.address.source))
                       ? engine::result<engine::component_description>{component}
                       : engine::prepare_component_editor_value(world, node, component);
      if (!value)
        return value.error();
      updates.push_back({object.address, component->guid(), std::move(*value), false, add});
    }
  }
  // 先拆开所有变化的父关系，避免合法的父子反转被旧树误判为循环。
  for (const auto& object : next->objects) {
    if (!old.contains(object.address) || old.at(object.address)->parent == object.parent)
      continue;
    auto status = world.node_reparent(nodes.at(object.address).node, {});
    if (status.failed())
      return status;
  }
  // 只销毁消失对象，从叶到根处理；其余实体保持原身份。
  std::vector<std::pair<std::size_t, engine::object_address>> removed;
  for (const auto& object : previous->objects) {
    if (desired.contains(object.address))
      continue;
    std::size_t depth = 0;
    auto parent = object.parent;
    while (parent.object) {
      auto found = old.find(parent);
      if (found == old.end() || ++depth > old.size())
        return engine::error::invalid_state;
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
  for (const auto& object : next->objects) {
    if (old.contains(object.address))
      continue;
    auto created = scenes.create_node(scene, {.uuid = object.address.source && !object.instance_root
                                                          ? engine::guid::generate()
                                                          : object.address.object,
                                              .name = object.effective.name,
                                              .local_transform = object.effective.transform});
    if (!created)
      return created.error();
    projected_object info;
    info.node = *created;
    associations[object.address] = {*created, entity_id{created->get()}};
    nodes.emplace(object.address, std::move(info));
  }
  for (const auto& object : next->objects) {
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
  for (const auto& update : updates) {
    const auto node = nodes.at(update.address).node;
    auto status = update.remove ? scenes.remove_component_description(scene, node, update.type)
                  : update.add  ? scenes.add_component_description(scene, node, update.value,
                                                                   update.value->uuid)
                                : scenes.set_component_description(scene, node, update.value);
    if (status.failed())
      return status;
  }
  // 资源身份来自文档快照；同 URI 的历史引用也不能被当前缓存替代。
  for (const auto& object : next->objects) {
    const auto* renderer = object.effective.components.get<engine::mesh_renderer_description>();
    if (!renderer)
      continue;
    engine::scene_mesh_binding target_binding;
    if (auto found = target.assets.find(renderer->mesh_uri); found != target.assets.end())
      target_binding.mesh = engine::resource_cast<engine::mesh>(found->second);
    if (auto found = target.assets.find(renderer->material_uri); found != target.assets.end())
      target_binding.material = engine::resource_cast<engine::material>(found->second);
    const auto node = nodes.at(object.address).node;
    auto current = scenes.capture_mesh_binding(scene, node);
    if (!current)
      return current.error();
    if (current->mesh == target_binding.mesh && current->material == target_binding.material)
      continue;
    auto status = scenes.replace_mesh_binding(
        scene, node, *current, target_binding,
        object.effective.components.identity(engine::mesh_renderer_description::type_guid));
    if (status.failed())
      return status;
  }
  for (const auto& object : next->objects) {
    if (!old.contains(object.address) ||
        same_transform(old.at(object.address)->effective.transform, object.effective.transform))
      continue;
    auto status =
        scenes.set_node_transform(scene, nodes.at(object.address).node, object.effective.transform);
    if (status.failed())
      return status;
  }
  return true;
}
}
engine::error project_scene_document(engine::world& world, engine::scene_instances& scenes,
                                     scene_instance_id scene, scene_projection& projection,
                                     const scene_document_snapshot* baseline,
                                     const scene_document_snapshot& target,
                                     bool prepare_editor) noexcept try {
  if (baseline) {
    auto associations = projection.objects();
    auto updated =
        update_objects(world, scenes, scene, associations, *baseline, target, prepare_editor);
    if (!updated)
      return updated.error();
    if (*updated) {
      projection.replace(std::move(associations));
      return engine::error::success;
    }
  }
  auto inputs = make_scene_document_inputs(target);
  if (!inputs)
    return inputs.error();
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  auto status = engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  auto rebuilt = scenes.replace_prepared(scene, std::move(prepared), target.assets);
  if (!rebuilt)
    return rebuilt.error();
  return projection.reset(rebuilt->objects);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
}
