// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/scene/prefab_property_override.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_object.hpp"
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
      if (!meta::has_flags(property.capabilities, meta::property_capabilities::writable))
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
      if (!source)
        return error::not_found;
      auto before = source->view(), after = value.view();
      if (!before)
        return before.error();
      if (!after)
        return after.error();
      auto pending = prefab->reference_.overrides;
      std::erase_if(pending, [&](const auto& v) {
        return v.key.source_node_uuid == member.address.source_node_uuid &&
               v.key.type_guid == value.guid();
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

error scene_instance::capture_mesh_binding(scene_node_id node,
                                           scene_mesh_binding& output) const noexcept {
  output = {};
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return error::invalid_handle;
  }
  auto renderer = native_world_.node_mesh_renderer(node);
  if (!renderer)
    return renderer.error() == error::not_found ? error::success : renderer.error();
  output = {.mesh = renderer->mesh, .material = renderer->material};
  return error::success;
}

error scene_instance::replace_mesh_binding(scene_node_id node_value,
                                           const scene_mesh_binding& expected,
                                           const scene_mesh_binding& replacement) noexcept try {
  if (engine::asset_kind(replacement.mesh) != asset_type::mesh ||
      engine::asset_kind(replacement.material) != asset_type::material ||
      !loader_.owns(replacement.mesh) || !loader_.owns(replacement.material)) {
    return error::invalid_argument;
  }
  const auto node = node_value;
  scene_mesh_binding current;
  const auto captured = capture_mesh_binding(node_value, current);
  if (captured != error::success) {
    return captured;
  }
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
      auto pending = prefab_reference(index).overrides;
      const std::array originals{renderer->mesh_uri, renderer->material_uri};
      const std::array replacements{std::string(asset_uri(replacement.mesh)),
                                    std::string(asset_uri(replacement.material))};
      for (std::size_t i = 0; i < originals.size(); ++i) {
        auto status = set_prefab_property_override(
            pending,
            {.key = {.source_node_uuid = member.address.source_node_uuid,
                     .type_guid = mesh_renderer_description::type_guid,
                     .property_id = meta::property_id{static_cast<std::uint32_t>(i + 1)}},
             .value = engine::value{replacements[i]}},
            engine::value{originals[i]});
        if (status.failed())
          return status;
      }
      const std::array changes{mesh_renderer_replacement{
          .entity = entity_id{scene_object_entity(native_world_, node)},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material}}};
      auto status = native_world_.replace_mesh_renderers(changes);
      if (status.failed())
        return status;
      prefab_reference(index).overrides.swap(pending);
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
  return std::ranges::find(objects_, node, &object::node) != objects_.end();
}
} // namespace gneiss::engine
