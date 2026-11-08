// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_projection.hpp"
#include "engine/function/scene/component_adapter.hpp"
namespace gneiss::editor {
namespace {
bool same_transform(const engine::math::transform& a, const engine::math::transform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
engine::result<bool> update_objects(engine::world& world, engine::scene_instances& scenes,
                                    scene_instance_id scene, const scene_document_snapshot& before,
                                    const scene_document_snapshot& target) {
  if (before.content.get().uuid != target.content.get().uuid || before.assets != target.assets)
    return false;
  auto previous = compose_scene_document(before);
  auto next = compose_scene_document(target);
  if (!previous)
    return previous.error();
  if (!next)
    return next.error();
  if (previous->objects.size() != next->objects.size())
    return false;
  std::map<document_object_address, const composed_document_object*> old;
  for (const auto& object : previous->objects)
    old.emplace(object.address, &object);
  for (const auto& object : next->objects) {
    auto found = old.find(object.address);
    if (found == old.end())
      return false;
    const auto& a = *found->second;
    if (a.prefab_uri != object.prefab_uri)
      return false;
    const bool hierarchy_changed =
        a.parent != object.parent || a.effective.name != object.effective.name;
    if (hierarchy_changed && object.address.source && !object.instance_root)
      return false;
    if (a.parent != object.parent && object.parent.object) {
      const auto parent = old.find(object.parent);
      if (parent == old.end() || (object.parent.source && !parent->second->instance_root))
        return false;
    }
  }
  std::map<document_object_address, engine::scene_node_info> nodes;
  auto count = scenes.get_node_count(scene);
  if (!count)
    return count.error();
  for (std::uint64_t i = 0; i < *count; ++i) {
    auto info = scenes.get_node_info(scene, i, engine::node_query_detail::components);
    if (!info)
      return info.error();
    auto address = info->prefab ? document_object_address{info->prefab->instance_uuid,
                                                          info->prefab->source_node_uuid}
                                : document_object_address{info->uuid, {}};
    nodes.emplace(address, *info);
  }
  // 所有目标及基线先检查，再开始更新；不把外部变更当作已同步状态。
  for (const auto& object : next->objects) {
    auto found = nodes.find(object.address);
    if (found == nodes.end() ||
        !same_transform(found->second.local_transform, old.at(object.address)->effective.transform))
      return false;
  }
  struct component_update {
    scene_node_id node;
    engine::guid type;
    engine::component_description value;
    bool remove{}, add{};
  };
  std::vector<component_update> updates;
  for (const auto& object : next->objects) {
    const auto& previous_components = old.at(object.address)->effective.components;
    const auto& components = object.effective.components;
    const auto node = nodes.at(object.address).node;
    for (const auto& previous_component : previous_components) {
      const auto* replacement = components.find(previous_component->guid());
      if (!replacement || (*replacement)->uuid != previous_component->uuid)
        updates.push_back({node, previous_component->guid(), {}, true, false});
    }
    for (const auto& component : components) {
      const auto* previous_component = previous_components.find(component->guid());
      const bool add = !previous_component || (*previous_component)->uuid != component->uuid;
      if (!add && engine::equal_components({*previous_component}, {component}))
        continue;
      auto value = add ? engine::result<engine::component_description>{component}
                       : engine::prepare_component_editor_value(world, node, component);
      if (!value)
        return value.error();
      updates.push_back({node, component->guid(), std::move(*value), false, add});
    }
  }
  // 先拆开所有变化的父关系，避免合法的父子反转被旧树误判为循环。
  for (const auto& object : next->objects) {
    if (old.at(object.address)->parent == object.parent)
      continue;
    auto status = scenes.reparent_node(scene, nodes.at(object.address).node, {});
    if (status.failed())
      return status;
  }
  for (const auto& object : next->objects) {
    const auto& previous_object = *old.at(object.address);
    const auto node = nodes.at(object.address).node;
    if (previous_object.parent != object.parent && object.parent.object) {
      auto status = scenes.reparent_node(scene, node, nodes.at(object.parent).node);
      if (status.failed())
        return status;
    }
    if (previous_object.effective.name != object.effective.name) {
      auto status = scenes.set_node_name(scene, node, object.effective.name);
      if (status.failed())
        return status;
    }
  }
  for (const auto& update : updates) {
    auto status =
        update.remove ? scenes.remove_component_description(scene, update.node, update.type)
        : update.add
            ? scenes.add_component_description(scene, update.node, update.value, update.value->uuid)
            : scenes.set_component_description(scene, update.node, update.value);
    if (status.failed())
      return status;
  }
  for (const auto& object : next->objects) {
    if (same_transform(old.at(object.address)->effective.transform, object.effective.transform))
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
                                     scene_instance_id scene,
                                     const scene_document_snapshot* baseline,
                                     const scene_document_snapshot& target) noexcept try {
  if (baseline) {
    auto updated = update_objects(world, scenes, scene, *baseline, target);
    if (!updated)
      return updated.error();
    if (*updated)
      return engine::error::success;
  }
  auto inputs = make_scene_document_inputs(target);
  if (!inputs)
    return inputs.error();
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  auto status = engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  return scenes.replace_prepared(scene, std::move(prepared), target.assets);
} catch (const std::bad_alloc&) {
  return engine::error::out_of_memory;
} catch (...) {
  return engine::error::internal;
}
}
