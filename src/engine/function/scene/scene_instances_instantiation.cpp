// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/function/scene/scene_types.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::engine {
result<scene_instance_id>
scene_instances::instantiate_prepared(scene_instantiation_data prepared,
                                      const resource_bindings& assets) noexcept {
  if (!is_valid()) {
    return engine::error::invalid_argument;
  }
  try {
    const auto find_asset = [&](std::string_view uri) {
      const auto found = assets.find(uri);
      return found == assets.end() ? engine::render_resource_ref{} : found->second;
    };
    for (const auto& [uri, source] : prepared.prefabs) {
      if (!source.resource)
        return error::invalid_argument;
    }
    auto instance = std::make_unique<scene_instance>(native_world_, loader_);
    instance->initialize_staged(prepared.description.uuid, prepared.description.objects.size());
    std::unordered_map<guid, scene_node_id, guid_hash> nodes;
    nodes.reserve(prepared.description.objects.size());
    for (const auto index : prepared.parent_first) {
      const auto& source = prepared.description.objects.at(index);
      if (!source.prefab) {
        const auto renderer = prepared_component_renderer(source.components, find_asset);
        const auto result = instance->create_staged_node(
            index, source, source.parent_uuid ? nodes.at(*source.parent_uuid) : null_scene_node_id,
            renderer.mesh, renderer.material);
        if (result != engine::error::success) {
          return result;
        }
        nodes.emplace(source.uuid, instance->staged_node(index));
        continue;
      }
      const auto lease = prepared.prefabs.at(source.prefab->uri).resource;
      prefab_description effective;
      auto prepared_status = prepare_prefab_reference(*lease, *source.prefab, effective);
      if (prepared_status.failed())
        return prepared_status;
      auto prefab = std::make_unique<prefab_binding>(native_world_, loader_, lease, source.uuid);
      auto result = prefab->create_root(source.parent_uuid ? nodes.at(*source.parent_uuid)
                                                           : null_scene_node_id,
                                        source.transform);
      if (result != engine::error::success) {
        return result;
      }
      scene_subtree_guard guard(native_world_, prefab->root());
      result = native_world_.node_set_identity(prefab->root(), {source.uuid, source.name});
      if (result.failed())
        return result;
      std::unordered_map<guid, scene_node_id, guid_hash> prefab_nodes;
      prefab_nodes.reserve(lease.get()->objects.size());
      for (const auto node_index : prepared.prefabs.at(source.prefab->uri).parent_first) {
        const auto& node = effective.objects.at(node_index);
        const auto renderer = prepared_component_renderer(node.components, find_asset);
        result = prefab->create_node(node_index, node,
                                     node.parent_uuid ? prefab_nodes.at(*node.parent_uuid)
                                                      : prefab->root(),
                                     renderer.mesh, renderer.material);
        if (result != engine::error::success) {
          return result;
        }
        prefab_binding::node_info info;
        result = prefab->get_node_info(node_index, info);
        if (result != engine::error::success) {
          return result;
        }
        prefab_nodes.emplace(node.uuid, info.node);
      }
      nodes.emplace(source.uuid, prefab->root());
      instance->append_staged_prefab(std::move(prefab), *source.prefab, index);
      guard.release();
    }
    auto created = instances_.create(resource_type::scene_instance, std::move(instance));
    if (!created)
      return created.error();
    return *created;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
result<scene_capture> scene_instances::capture_prepared(scene_instance_id instance) const noexcept
    try {
  const auto* value = instances_.get(instance, resource_type::scene_instance);
  if (!value || !*value)
    return error::invalid_handle;
  auto captured = (*value)->capture_description();
  if (!captured)
    return captured.error();
  auto scene = std::make_shared<scene_description>(std::move(*captured));
  scene_preparation_inputs inputs;
  inputs.scene = asset_ref<const scene_description>{scene};
  for (const auto& prefab : scene->objects) {
    if (!prefab.prefab)
      continue;
    auto header = (*value)->capture_prefab_instance_header(prefab.uuid);
    if (!header)
      return header.error();
    inputs.prefabs.insert_or_assign(prefab.prefab->uri, std::move(header->source));
  }
  scene_instantiation_data prepared;
  scene_diagnostic diagnostic;
  auto status = prepare_loaded_scene(inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  resource_bindings bindings;
  for (const auto& source : prepared.assets) {
    auto loaded = loader_.find(source);
    if (!loaded)
      return loaded.error();
    bindings.emplace(source.uri, std::move(*loaded));
  }
  return scene_capture{std::move(prepared), std::move(bindings)};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

} // namespace gneiss::engine
