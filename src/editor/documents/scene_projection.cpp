// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_projection.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include <algorithm>
#include <set>
namespace gneiss::editor {
engine::error scene_projection::refresh(engine::scene_instances& scenes,
                                        scene_instance_id scene) noexcept try {
  std::map<document_object_address, object_binding> next;
  auto count = scenes.get_node_count(scene);
  if (!count) {
    objects_.clear();
    return count.error();
  }
  for (std::uint64_t index = 0; index < *count; ++index) {
    auto info = scenes.get_node_info(scene, index, engine::node_query_detail::identity);
    if (!info) {
      objects_.clear();
      return info.error();
    }
    const auto address = info->prefab ? document_object_address{info->prefab->instance_uuid,
                                                                info->prefab->source_node_uuid}
                                      : document_object_address{info->uuid, {}};
    if (!next.emplace(address, object_binding{info->node, info->entity}).second) {
      objects_.clear();
      return engine::error::invalid_state;
    }
  }
  objects_.swap(next);
  return engine::error::success;
} catch (const std::bad_alloc&) {
  objects_.clear();
  return engine::error::out_of_memory;
} catch (...) {
  objects_.clear();
  return engine::error::internal;
}
namespace {
bool same_transform(const engine::math::transform& a, const engine::math::transform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
engine::result<bool> update_objects(engine::world& world, engine::scene_instances& scenes,
                                    scene_instance_id scene, const scene_document_snapshot& before,
                                    const scene_document_snapshot& target, bool prepare_editor) {
  if (before.content.get().uuid != target.content.get().uuid)
    return false;
  auto previous = compose_scene_document(before);
  auto next = compose_scene_document(target);
  if (!previous)
    return previous.error();
  if (!next)
    return next.error();
  // 整个 Prefab 删除只清理它的子树；其他实例和普通对象保留。
  std::vector<engine::guid> removed_prefabs;
  for (const auto& root : previous->objects)
    if (root.instance_root && std::ranges::none_of(next->objects, [&](const auto& object) {
          return object.address.object == root.address.object && object.address.source;
        }))
      removed_prefabs.push_back(root.address.object);
  for (auto uuid : removed_prefabs) {
    std::set<document_object_address> removed;
    for (const auto& object : previous->objects)
      if (object.address.object == uuid && object.address.source)
        removed.insert(object.address);
    if (removed.empty())
      continue;
    for (bool changed = true; changed;) {
      changed = false;
      for (const auto& object : previous->objects)
        if (removed.contains(object.parent))
          changed |= removed.insert(object.address).second;
    }
    // 删除实例时，自有对象可以在同一编辑中移到实例外；先脱离再删除来源子树。
    for (auto& object : previous->objects) {
      if (!removed.contains(object.address) ||
          std::ranges::find(next->objects, object.address, &composed_document_object::address) ==
              next->objects.end())
        continue;
      if (!object.address.source || object.instance_root) {
        auto node = scenes.find_node(scene, object.address.object);
        if (!node)
          return node.error();
        auto status = scenes.reparent_node(scene, *node, {});
        if (status.failed())
          return status;
        object.parent = {};
      }
      removed.erase(object.address);
    }
    auto status = scenes.remove_prefab_instance(scene, uuid);
    if (status.failed())
      return status;
    std::erase_if(previous->objects,
                  [&](const auto& object) { return removed.contains(object.address); });
  }
  // 普通对象转为 Prefab 时，其旧角色先删除，自有子对象稍后连接到新根。
  for (const auto& root : next->objects) {
    if (!root.instance_root)
      continue;
    auto old =
        std::ranges::find(previous->objects, document_object_address{root.address.object, {}},
                          &composed_document_object::address);
    if (old == previous->objects.end())
      continue;
    const auto address = old->address;
    for (auto& child : previous->objects) {
      if (child.parent != address || (child.address.source && !child.instance_root))
        continue;
      auto node = scenes.find_node(scene, child.address.object);
      if (!node)
        return node.error();
      auto status = scenes.reparent_node(scene, *node, {});
      if (status.failed())
        return status;
      child.parent = {};
    }
    auto node = scenes.find_node(scene, address.object);
    if (!node)
      return node.error();
    auto status = scenes.destroy_node(scene, *node);
    if (status.failed())
      return status;
    std::erase_if(previous->objects, [&](const auto& object) { return object.address == address; });
  }
  // 混合子树先创建普通对象的空壳，Prefab 可以引用同次编辑新增的父节点。
  for (const auto& object : next->objects) {
    if (object.address.source ||
        std::ranges::find(previous->objects, object.address, &composed_document_object::address) !=
            previous->objects.end())
      continue;
    auto created = scenes.create_node(scene, {.uuid = object.effective.uuid,
                                              .name = object.effective.name,
                                              .local_transform = object.effective.transform});
    if (!created)
      return created.error();
    auto baseline = object;
    baseline.parent = {};
    baseline.effective.components = {};
    previous->objects.push_back(std::move(baseline));
  }
  // 新增或结构变化只创建/替换对应 Prefab，其他实例和普通对象保持。
  auto description = target.description();
  if (!description)
    return description.error();
  std::vector<const composed_document_object*> roots;
  for (const auto& object : next->objects)
    if (object.instance_root)
      roots.push_back(&object);
  const auto depth = [&](const composed_document_object* object) {
    std::size_t result = 0;
    while (object->parent.object && result <= next->objects.size()) {
      auto parent =
          std::ranges::find(next->objects, object->parent, &composed_document_object::address);
      if (parent == next->objects.end())
        break;
      object = &*parent;
      ++result;
    }
    return result;
  };
  std::ranges::stable_sort(roots, {}, depth);
  for (const auto* root_ptr : roots) {
    const auto& root = *root_ptr;
    const bool existing = std::ranges::any_of(previous->objects, [&](const auto& object) {
      return object.address.object == root.address.object && object.address.source;
    });
    const auto belongs = [&](const auto& object) {
      return object.address.object == root.address.object && object.address.source;
    };
    const bool structural_change =
        existing &&
        (std::ranges::any_of(previous->objects,
                             [&](const auto& old) {
                               if (!belongs(old))
                                 return false;
                               auto found = std::ranges::find(next->objects, old.address,
                                                              &composed_document_object::address);
                               return found == next->objects.end() ||
                                      old.prefab_uri != found->prefab_uri ||
                                      (!old.instance_root &&
                                       (old.parent != found->parent ||
                                        old.effective.name != found->effective.name));
                             }) ||
         std::ranges::any_of(next->objects, [&](const auto& item) {
           return belongs(item) &&
                  std::ranges::find(previous->objects, item.address,
                                    &composed_document_object::address) == previous->objects.end();
         }));
    if (existing && !structural_change)
      continue;
    document_object_address previous_root;
    if (existing)
      for (const auto& item : previous->objects)
        if (belongs(item) && item.instance_root)
          previous_root = item.address;
    const auto author = std::ranges::find(description->objects, root.address.object,
                                          &engine::game_object_description::uuid);
    if (author == description->objects.end() || !author->prefab)
      return engine::error::invalid_state;
    if (author->parent_uuid && !scenes.find_node(scene, author->parent_uuid))
      return false;
    const auto& frozen = target.prefabs.at(author->prefab->uri);
    auto source = engine::restore_prefab_record(frozen.content);
    if (!source)
      return source.error();
    auto replayed = engine::replay_prefab_reference(*source, *author->prefab);
    if (!replayed)
      return replayed.error();
    std::vector<engine::scene_mesh_binding> bindings;
    for (const auto& object : replayed->description.objects) {
      engine::scene_mesh_binding binding;
      if (const auto* renderer = object.components.get<engine::mesh_renderer_description>()) {
        if (auto found = target.assets.find(renderer->mesh_uri); found != target.assets.end())
          binding.mesh = engine::resource_cast<engine::mesh>(found->second);
        if (auto found = target.assets.find(renderer->material_uri); found != target.assets.end())
          binding.material = engine::resource_cast<engine::material>(found->second);
      }
      bindings.push_back(std::move(binding));
    }
    auto created = scenes.instantiate_prefab_ready(
        scene, *author, engine::make_ref<const engine::prefab_description>(std::move(*source)),
        bindings, existing, frozen.source_asset, engine::prefab_replacement::preserve_nodes);
    if (!created)
      return created.error();
    if (existing) {
      std::erase_if(previous->objects, belongs);
      for (auto& object : previous->objects)
        if (object.parent == previous_root)
          object.parent = root.address;
    }
    for (const auto& object : next->objects)
      if (object.address.object == root.address.object && object.address.source)
        previous->objects.push_back(object);
  }
  std::map<document_object_address, const composed_document_object*> old;
  for (const auto& object : previous->objects)
    old.emplace(object.address, &object);
  std::map<document_object_address, const composed_document_object*> desired;
  for (const auto& object : next->objects)
    desired.emplace(object.address, &object);
  for (const auto& object : previous->objects)
    if (!desired.contains(object.address) && object.address.source)
      return false;
  for (const auto& object : next->objects) {
    auto found = old.find(object.address);
    if (found == old.end()) {
      if (object.address.source)
        return false;
    } else {
      const auto& a = *found->second;
      if (a.prefab_uri != object.prefab_uri)
        return false;
      const bool hierarchy_changed =
          a.parent != object.parent || a.effective.name != object.effective.name;
      if (hierarchy_changed && object.address.source && !object.instance_root)
        return false;
    }
    if ((found == old.end() || found->second->parent != object.parent) && object.parent.object) {
      const auto parent = desired.find(object.parent);
      if (parent == desired.end() || (object.parent.source && !parent->second->instance_root))
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
  if (nodes.size() != old.size())
    return false;
  for (const auto& object : previous->objects) {
    auto found = nodes.find(object.address);
    if (found == nodes.end() ||
        !same_transform(found->second.local_transform, old.at(object.address)->effective.transform))
      return false;
  }
  struct component_update {
    document_object_address address;
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
    auto status = scenes.reparent_node(scene, nodes.at(object.address).node, {});
    if (status.failed())
      return status;
  }
  // 只销毁消失对象，从叶到根处理；其余实体保持原身份。
  std::vector<std::pair<std::size_t, document_object_address>> removed;
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
    auto status = scenes.destroy_node(scene, nodes.at(address).node);
    if (status.failed())
      return status;
    nodes.erase(address);
  }
  // 新对象先脱离层级创建，随后统一连接，描述排列不必父节点在前。
  for (const auto& object : next->objects) {
    if (old.contains(object.address))
      continue;
    auto created = scenes.create_node(scene, {.uuid = object.effective.uuid,
                                              .name = object.effective.name,
                                              .local_transform = object.effective.transform});
    if (!created)
      return created.error();
    engine::scene_node_info info;
    info.node = *created;
    nodes.emplace(object.address, std::move(info));
  }
  for (const auto& object : next->objects) {
    auto previous_object = old.find(object.address);
    const auto node = nodes.at(object.address).node;
    if ((previous_object == old.end() || previous_object->second->parent != object.parent) &&
        object.parent.object) {
      auto status = scenes.reparent_node(scene, node, nodes.at(object.parent).node);
      if (status.failed())
        return status;
    }
    if (previous_object != old.end() &&
        previous_object->second->effective.name != object.effective.name) {
      auto status = scenes.set_node_name(scene, node, object.effective.name);
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
                                     scene_instance_id scene,
                                     const scene_document_snapshot* baseline,
                                     const scene_document_snapshot& target,
                                     bool prepare_editor) noexcept try {
  if (baseline) {
    auto updated = update_objects(world, scenes, scene, *baseline, target, prepare_editor);
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
