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
      auto pending = prefab_author(index).prefab->overrides;
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
      prefab_author(index).prefab->overrides.swap(pending);
      return error::success;
    }
  }
  const auto found = find_ordinary_node(node);
  if (found == objects_.end())
    return error::invalid_handle;
  mesh_renderer_description author{
      .mesh_uri = std::string(engine::asset_uri(replacement.mesh)),
      .material_uri = std::string(engine::asset_uri(replacement.material)),
  };
  const std::array changes{
      engine::mesh_renderer_replacement{
          .entity = entity_id{scene_object_entity(native_world_, found->node)},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material},
      },
  };
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  auto pending = description_.objects[index].components;
  pending.set(std::move(author));
  auto changed = error::success;
  if (!expected.mesh && !expected.material &&
      !description_.objects[index].components.template get<mesh_renderer_description>()) {
    changed = native_world_.entity_set_mesh_renderer(
        entity_id{scene_object_entity(native_world_, found->node)},
        {.mesh = replacement.mesh, .material = replacement.material});
  } else {
    changed = native_world_.replace_mesh_renderers(changes);
  }
  if (changed != error::success) {
    return changed;
  }
  description_.objects[index].components = std::move(pending);
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
