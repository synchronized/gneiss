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
#include <unordered_set>

namespace gneiss::engine {
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
          state.component_uuid = change.component_uuid;
          state.removed = !change.added;
        }
      for (const auto& change : prefab->reference_.overrides)
        if (change.key.target.source_node_uuid == source &&
            component_description_type(change.key.target.target_type_guid) == type)
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
      (state.added && (!*state.added || (*state.added)->guid() != type)))
    return error::invalid_argument;
  for (auto& prefab : prefab_instances_)
    for (const auto& member : prefab->nodes_) {
      if (member.node != node)
        continue;
      const auto source = member.address.source_node_uuid;
      auto candidate =
          prepare_prefab_component_override(*prefab->prefab_, prefab->reference_, source, type,
                                            state.added ? *state.added : component_description{},
                                            state.removed, state.properties, state.component_uuid);
      if (!candidate)
        return candidate.error();
      const auto& effective = candidate->effective;
      auto status = error{error::success};
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
        auto projected = capture_components(native_world_, node);
        if (!projected)
          return projected.error();
        auto entries = projected->entries();
        std::erase_if(entries,
                      [&](const auto& entry) { return entry.description->guid() == type; });
        entries.push_back({target->components.identity(type), *value});
        status = projected->replace(std::move(entries));
        if (status.ok())
          status = replace_components({native_world_, node, renderer, &loader_}, *projected);
      } else {
        auto current = capture_components(native_world_, node);
        if (!current)
          return current.error();
        status = current->find(type) ? remove_component({native_world_, node}, type)
                                     : error{error::success};
      }
      if (status.failed())
        return status;
      prefab->reference_ = std::move(candidate->reference);
      prefab->conflicts_ = std::move(candidate->conflicts);
      return error::success;
    }
  return error::unsupported;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error scene_instance::add_component_description(scene_node_id node,
                                                const component_description& value,
                                                guid identity) noexcept {
  if (!value)
    return error::invalid_argument;
  if (!contains_editable_node(node))
    return error::invalid_handle;
  if (find_ordinary_node(node) == objects_.end()) {
    auto current = capture_components(native_world_, node);
    if (!current)
      return current.error();
    if (current->find(value->guid()))
      return error::invalid_state;
    return set_component_override(node, value->guid(),
                                  {.added = value, .component_uuid = identity});
  }
  return add_component({native_world_, node, {}, &loader_}, value, identity);
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
  if (!value)
    return error::invalid_argument;
  if (!contains_editable_node(node))
    return error::invalid_handle;
  auto components = capture_components(native_world_, node);
  if (!components)
    return components.error();
  if (!components->find(value->guid()))
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
      auto prepared = prepare_prefab_component_edit(*prefab->prefab_, prefab->reference_,
                                                    member.address.source_node_uuid,
                                                    *components->find(value->guid()), value);
      if (!prepared)
        return prepared.error();
      auto status = apply();
      if (status.failed())
        return status;
      prefab->reference_ = std::move(prepared->reference);
      prefab->conflicts_ = std::move(prepared->conflicts);
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
      auto pending = prepare_mesh_renderer_overrides(
          member.address.source_node_uuid,
          source->components.identity(mesh_renderer_description::type_guid), *renderer,
          {std::string(resource_uri(replacement.mesh)),
           std::string(resource_uri(replacement.material))},
          prefab_reference(index).overrides);
      if (!pending)
        return pending.error();
      auto reference = prefab.reference_;
      reference.overrides = *pending;
      auto replayed = replay_prefab_reference(*prefab.prefab_, reference);
      if (!replayed)
        return replayed.error();
      const std::array changes{mesh_renderer_replacement{
          .entity = entity_id{scene_object_entity(native_world_, node)},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material}}};
      auto status = native_world_.replace_mesh_renderers(changes);
      if (status.failed())
        return status;
      prefab_reference(index).overrides.swap(*pending);
      prefab.conflicts_ = std::move(replayed->conflicts);
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
