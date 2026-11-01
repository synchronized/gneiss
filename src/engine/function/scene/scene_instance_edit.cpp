// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/function/world/component_identity.hpp"
#include <algorithm>
#include <array>
#include <new>

namespace gneiss::engine {
namespace {
error collect_component_overrides(meta::instance source, meta::instance candidate,
                                  prefab_property_override_key key,
                                  std::vector<prefab_property_override>& output,
                                  unsigned depth = 0) {
  if (depth > 64 || source.type_id() != candidate.type_id())
    return error::invalid_argument;
  if (source.kind() == meta::type_kind::object) {
    auto properties = source.properties();
    if (!properties)
      return properties.error();
    for (const auto& property : *properties) {
      // 派生运行状态仍可保存，但不因普通组件编辑生成实例属性覆盖。
      if (!meta::has_flags(property.capabilities, meta::property_capabilities::writable) ||
          meta::has_flags(property.flags, meta::property_flags::editor_read_only))
        continue;
      auto left = source.property(property.id), right = candidate.property(property.id);
      if (!left)
        return left.error();
      if (!right)
        return right.error();
      auto next = key;
      if (!next.property_id.is_valid())
        next.property_id = property.id;
      else
        next.path.emplace_back(property.id);
      auto status = collect_component_overrides(*left, *right, std::move(next), output, depth + 1);
      if (status.failed())
        return status;
    }
    return error::success;
  }
  if (source.kind() == meta::type_kind::sequence) {
    auto left = source.as_sequence(), right = candidate.as_sequence();
    if (!left)
      return left.error();
    if (!right)
      return right.error();
    auto count = left->size(), other = right->size();
    if (!count)
      return count.error();
    if (!other)
      return other.error();
    if (*count != *other)
      return error::unsupported;
    for (std::size_t i = 0; i < *count; ++i) {
      auto a = left->at(i), b = right->at(i);
      if (!a)
        return a.error();
      if (!b)
        return b.error();
      auto next = key;
      next.path.emplace_back(i);
      auto status = collect_component_overrides(*a, *b, std::move(next), output, depth + 1);
      if (status.failed())
        return status;
    }
    return error::success;
  }
  auto before = source.read_value(), after = candidate.read_value();
  if (!before)
    return before.error();
  if (!after)
    return after.error();
  return set_prefab_property_override(output, {std::move(key), *after}, *before);
}
}
result<prefab_component_state> scene_instance::capture_component_override(scene_node_id node,
                                                                          guid type) noexcept try {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  for (auto& prefab : prefab_instances_)
    for (const auto& member : prefab->nodes_) {
      if (member.node != node)
        continue;
      prefab_component_state state;
      const auto source = member.address.source_node_uuid;
      for (const auto& change : prefab->reference_.component_overrides)
        if (change.source_node_uuid == source && change.type_guid == type) {
          state.added = change.added;
          state.removed = !change.added;
        }
      for (const auto& change : prefab->reference_.overrides)
        if (change.key.source_node_uuid == source &&
            component_description_type(change.key.type_guid) == type)
          state.properties.push_back(change);
      if (type == mesh_renderer_description::type_guid) {
        auto resources = capture_mesh_binding(node);
        if (!resources)
          return resources.error();
        state.resources = std::move(*resources);
      }
      return state;
    }
  return error::unsupported;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error scene_instance::set_component_override(scene_node_id node, guid type,
                                             const prefab_component_state& state) noexcept try {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  if ((state.added && state.removed) ||
      ((state.added || state.removed) && !state.properties.empty()) ||
      (state.added && state.added->guid() != type))
    return error::invalid_argument;
  for (auto& prefab : prefab_instances_)
    for (const auto& member : prefab->nodes_) {
      if (member.node != node)
        continue;
      const auto source = member.address.source_node_uuid;
      auto pending = prefab->reference_;
      std::erase_if(pending.component_overrides, [&](const auto& v) {
        return v.source_node_uuid == source && v.type_guid == type;
      });
      std::erase_if(pending.overrides, [&](const auto& v) {
        return v.key.source_node_uuid == source &&
               component_description_type(v.key.type_guid) == type;
      });
      if (state.added || state.removed)
        pending.component_overrides.push_back({source, type, state.added});
      for (const auto& value : state.properties) {
        if (value.key.source_node_uuid != source ||
            component_description_type(value.key.type_guid) != type)
          return error::invalid_argument;
        pending.overrides.push_back(value);
      }
      prefab_description effective;
      auto status = prepare_prefab_reference(*prefab->prefab_, pending, effective);
      if (status.failed())
        return status;
      auto target = std::ranges::find(effective.objects, source, &object_description::uuid);
      if (target == effective.objects.end())
        return error::not_found;
      const auto* value = target->components.find(type);
      if (value) {
        mesh_renderer renderer{state.resources.mesh, state.resources.material};
        if (type == mesh_renderer_description::type_guid) {
          if (!renderer.mesh || !renderer.material) {
            auto ready = resolve_component_renderer({*value}, loader_);
            if (!ready)
              return ready.error();
            renderer = *ready;
          }
          if ((renderer.mesh && !loader_.owns(renderer.mesh)) ||
              (renderer.material && !loader_.owns(renderer.material)))
            return error::invalid_argument;
        }
        status = install_components({native_world_, node, renderer, &loader_}, {*value});
      } else {
        auto current = capture_components(native_world_, node);
        if (!current)
          return current.error();
        status = current->find(type) ? remove_component({native_world_, node}, type)
                                     : error{error::success};
      }
      if (status.failed())
        return status;
      prefab->reference_ = std::move(pending);
      return error::success;
    }
  return error::unsupported;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error scene_instance::add_component_description(scene_node_id node,
                                                const component_description& value) noexcept {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  if (find_ordinary_node(node) == objects_.end()) {
    auto current = capture_components(native_world_, node);
    if (!current)
      return current.error();
    if (current->find(value.guid()))
      return error::invalid_state;
    return set_component_override(node, value.guid(), {.added = value});
  }
  return add_component({native_world_, node, {}, &loader_}, value);
}
error scene_instance::remove_component_description(scene_node_id node, guid type) noexcept {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  if (find_ordinary_node(node) == objects_.end()) {
    auto current = capture_components(native_world_, node);
    if (!current)
      return current.error();
    if (!current->find(type))
      return error::not_found;
    auto state = capture_component_override(node, type);
    if (!state)
      return state.error();
    return set_component_override(node, type, {.removed = !state->added});
  }
  return remove_component({native_world_, node, {}, &loader_}, type);
}
error scene_instance::set_component_description(scene_node_id node,
                                                const component_description& value) noexcept try {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  auto components = capture_components(native_world_, node);
  if (!components)
    return components.error();
  if (!components->find(value.guid()))
    return error::not_found;
  auto renderer = native_world_.node_mesh_renderer(node);
  if (!renderer && renderer.error() != error::not_found)
    return renderer.error();
  component_context context{native_world_, node, renderer ? *renderer : mesh_renderer{}, &loader_};
  const auto apply = [&] { return install_components(context, component_descriptions{value}); };
  for (auto& prefab : prefab_instances_) {
    for (const auto& member : prefab->nodes_) {
      if (member.node != node)
        continue;
      const auto* object = prefab->find_source_object(member.address.source_node_uuid);
      const auto* source = object ? object->components.find(value.guid()) : nullptr;
      if (!source) {
        auto state = capture_component_override(node, value.guid());
        if (!state)
          return state.error();
        if (!state->added)
          return error::not_found;
        state->added = value;
        return set_component_override(node, value.guid(), *state);
      }
      auto before = source->view(), after = value.view();
      if (!before)
        return before.error();
      if (!after)
        return after.error();
      auto pending = prefab->reference_.overrides;
      std::erase_if(pending, [&](const auto& v) {
        return v.key.source_node_uuid == member.address.source_node_uuid &&
               component_description_type(v.key.type_guid) == value.guid();
      });
      auto status = collect_component_overrides(
          *before, *after,
          {.source_node_uuid = member.address.source_node_uuid, .type_guid = value.guid()},
          pending);
      if (status.failed())
        return status;
      status = apply();
      if (status.failed())
        return status;
      prefab->reference_.overrides.swap(pending);
      return error::success;
    }
  }
  return apply();
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_mesh_binding> scene_instance::capture_mesh_binding(scene_node_id node) const noexcept {
  const auto found = std::ranges::find(objects_, node, &node_entry::node);
  if (found == objects_.end()) {
    return error::invalid_handle;
  }
  auto renderer = native_world_.node_mesh_renderer(node);
  if (!renderer)
    return renderer.error() == error::not_found ? result<scene_mesh_binding>{scene_mesh_binding{}}
                                                : result<scene_mesh_binding>{renderer.error()};
  return scene_mesh_binding{renderer->mesh, renderer->material};
}

error scene_instance::replace_mesh_binding(scene_node_id node_value,
                                           const scene_mesh_binding& expected,
                                           const scene_mesh_binding& replacement) noexcept try {
  if ((replacement.mesh && !loader_.owns(replacement.mesh)) ||
      (replacement.material && !loader_.owns(replacement.material))) {
    return error::invalid_argument;
  }
  const auto node = node_value;
  const auto captured = capture_mesh_binding(node_value);
  if (!captured)
    return captured.error();
  const auto& current = *captured;
  if (current.mesh != expected.mesh || current.material != expected.material) {
    return error::invalid_state;
  }
  // 实例资源引用保存为稀疏覆盖，只有绑定成功后才更新作者数据。
  for (std::size_t index = 0; index < prefab_instances_.size(); ++index) {
    auto& prefab = *prefab_instances_[index];
    for (const auto& member : prefab.nodes_) {
      if (member.node != node)
        continue;
      const auto* source = prefab.find_source_object(member.address.source_node_uuid);
      const auto* renderer = source ? source->components.get<mesh_renderer_description>() : nullptr;
      if (!renderer)
        return error::not_found;
      auto pending =
          prepare_mesh_renderer_overrides(member.address.source_node_uuid, *renderer,
                                          {std::string(resource_uri(replacement.mesh)),
                                           std::string(resource_uri(replacement.material))},
                                          prefab_reference(index).overrides);
      if (!pending)
        return pending.error();
      const std::array changes{mesh_renderer_replacement{
          .entity = entity_id{scene_object_entity(native_world_, node)},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material}}};
      auto status = native_world_.replace_mesh_renderers(changes);
      if (status.failed())
        return status;
      prefab_reference(index).overrides.swap(*pending);
      return error::success;
    }
  }
  const auto found = find_ordinary_node(node);
  if (found == objects_.end())
    return error::invalid_handle;
  const std::array changes{
      engine::mesh_renderer_replacement{
          .entity = entity_id{scene_object_entity(native_world_, found->node)},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material},
      },
  };
  auto existing = native_world_.node_mesh_renderer(node);
  if (!existing && existing.error() != error::not_found)
    return existing.error();
  auto changed = error::success;
  if (!existing) {
    changed = native_world_.entity_set_mesh_renderer(
        entity_id{scene_object_entity(native_world_, found->node)},
        {.mesh = replacement.mesh, .material = replacement.material});
  } else {
    changed = native_world_.replace_mesh_renderers(changes);
  }
  if (changed != error::success) {
    return changed;
  }
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

bool scene_instance::contains_editable_node(scene_node_id node) const noexcept {
  return std::ranges::find(objects_, node, &node_entry::node) != objects_.end();
}
} // namespace gneiss::engine
