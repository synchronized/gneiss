// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/scene/scene_update.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <algorithm>
#include <set>
namespace gneiss {
namespace {
bool same_transform(const math::transform& a, const math::transform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}
} // namespace
result<void>
rebase_scene_update(world& world,
                    const std::map<object_address, scene_object_association>& associations,
                    composed_scene& previous, composed_scene& next) {
  if (!world.is_owner_thread())
    return error::invalid_state;
  // 来源未修改的变换保留游戏运行值；来源修改才覆盖该值。
  for (auto& object : previous.objects) {
    const auto binding = associations.find(object.address);
    if (binding == associations.end())
      return error::invalid_state;
    auto runtime = world.object(binding->second.entity);
    if (!runtime)
      return runtime.error();
    auto transform = runtime->local_transform();
    if (!transform)
      return transform.error();
    auto replacement = std::ranges::find(next.objects, object.address, &composed_object::address);
    if (replacement != next.objects.end() &&
        same_transform(object.effective.transform, replacement->effective.transform))
      replacement->effective.transform = *transform;
    object.effective.transform = *transform;
    // 用实际运行状态比较目标，恢复来源指定的名称、组件和父关系。
    const std::string name(runtime->name());
    auto components = capture_components(world, binding->second.entity);
    if (!components)
      return components.error();
    object.effective.name = name;
    object.effective.components = std::move(*components);
    object_address parent_address;
    auto parent_object = runtime->parent();
    if (!parent_object)
      return parent_object.error();
    const entity_id parent_node{parent_object->entity().get()};
    if (parent_node) {
      auto parent = std::ranges::find_if(
          associations, [&](const auto& entry) { return entry.second.entity == parent_node; });
      if (parent == associations.end())
        return error::invalid_state;
      parent_address = parent->first;
    }
    object.parent = parent_address;
  }
  return {};
}
result<bool> apply_scene(scene_instance destination,
                         std::map<object_address, scene_object_association>& associations,
                         const composed_scene& previous, const composed_scene& next,
                         const scene_resource_bindings& assets,
                         const scene_component_preparer& prepare) {
  if (!destination.owner())
    return error::invalid_handle;
  auto& world = *destination.owner();
  auto count = destination.size();
  if (!count)
    return count.error();
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
    entity_id node;
    math::transform local_transform;
  };
  std::map<object_address, projected_object> nodes;
  std::set<std::uint64_t> entities;
  for (const auto& [address, binding] : associations) {
    if (!entities.insert(binding.entity.get()).second)
      return error::invalid_argument;
    auto borrowed = world.object(binding.entity);
    if (!borrowed)
      return false;
    if (!destination.contains(*borrowed))
      return false;
    auto transform = borrowed->local_transform();
    if (!transform)
      return transform.error();
    projected_object info;
    info.node = binding.entity;
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
    const auto node = previous_object == old.end() ? entity_id{} : nodes.at(object.address).node;
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
    auto status = world.find(gneiss::entity_id{(nodes.at(object.address).node).get()})
                      .reparent(gneiss::game_object{});
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
    auto status = world.find(gneiss::entity_id{(nodes.at(address).node).get()}).destroy();
    if (status.failed())
      return status;
    nodes.erase(address);
    associations.erase(address);
  }
  // 新对象先脱离层级创建，随后统一连接，描述排列不必父节点在前。
  for (const auto& object : next.objects) {
    if (old.contains(object.address))
      continue;
    auto created = destination.create_object({object.effective.name, object.effective.transform});
    if (!created)
      return created.error();
    projected_object info;
    info.node = entity_id{created->entity().get()};
    associations[object.address] = {created->entity()};
    nodes.emplace(object.address, std::move(info));
  }
  for (const auto& object : next.objects) {
    auto previous_object = old.find(object.address);
    const auto node = nodes.at(object.address).node;
    if ((previous_object == old.end() || previous_object->second->parent != object.parent) &&
        object.parent.object) {
      auto status =
          world.find(gneiss::entity_id{(node).get()})
              .reparent(world.find(gneiss::entity_id{(nodes.at(object.parent).node).get()}));
      if (status.failed())
        return status;
    }
    if (previous_object != old.end() &&
        previous_object->second->effective.name != object.effective.name) {
      auto status = world.find(entity_id{node.get()}).set_name(object.effective.name);
      if (status.failed())
        return status;
    }
  }
  const auto prepared_binding = [&](const composed_object& object) {
    mesh_component binding;
    if (const auto* renderer = object.effective.components.get<mesh_component_description>()) {
      if (auto found = assets.find(renderer->mesh_uri); found != assets.end())
        binding.mesh = resource_cast<mesh>(found->second);
      if (auto found = assets.find(renderer->material_uri); found != assets.end())
        binding.material = resource_cast<material>(found->second);
    }
    return binding;
  };
  for (const auto& update : updates) {
    const auto node = nodes.at(update.address).node;
    auto target = world.object(entity_id{node.get()});
    if (!target)
      return target.error();
    component_context context{
        *target, [&](const render_resource_source& source) -> result<render_resource_ref> {
          auto found = assets.find(source.uri);
          return found == assets.end() ? result<render_resource_ref>{error::not_ready}
                                       : result<render_resource_ref>{found->second};
        }};
    auto status = update.remove ? remove_component(context, update.type)
                  : update.add  ? add_component(context, update.value, update.value->uuid)
                                : update_component(context, update.value);
    if (status.failed())
      return status;
  }
  // 使用调用方提供的资源身份，不用当前缓存替换同 URI 引用。
  for (const auto& object : next.objects) {
    const auto* renderer = object.effective.components.get<mesh_component_description>();
    if (!renderer)
      continue;
    const auto target_binding = prepared_binding(object);
    const auto node = nodes.at(object.address).node;
    auto target = world.find(node);
    if (!destination.contains(target))
      return error::invalid_handle;
    auto current = target.try_component<mesh_component>();
    if (current && current->mesh == target_binding.mesh &&
        current->material == target_binding.material)
      continue;
    // 运行实体已通过本次场景归属检查；资源身份来自调用方固定内容。
    // 已有组件只更新资源引用，保留组件身份；缺失组件走通用装配入口。
    error status;
    if (current) {
      status = target.modify_component<mesh_component>(
          [&](auto& component) noexcept { component = target_binding; });
    } else {
      component_context context{
          target, [&](const render_resource_source& source) -> result<render_resource_ref> {
            auto found = assets.find(source.uri);
            return found == assets.end() ? result<render_resource_ref>{error::not_ready}
                                         : result<render_resource_ref>{found->second};
          }};
      status = add_component(
          context, make_component_description(*renderer),
          object.effective.components.identity(mesh_component_description::type_guid));
    }
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
  for (const auto& object : next.objects) {
    auto target = world.object(entity_id{nodes.at(object.address).node.get()});
    if (!target)
      return target.error();
    component_context context{*target};
    context.lookup_object = [&](object_address address) -> result<game_object> {
      auto found = associations.find(address);
      return found == associations.end() ? result<game_object>{error::not_found}
                                         : world.object(found->second.entity);
    };
    auto status = resolve_component_references(context, object.effective.components);
    if (status.failed())
      return status;
  }
  return true;
}
} // namespace gneiss
