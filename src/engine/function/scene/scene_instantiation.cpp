// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::engine {
engine::error scene_instances::instantiate_prepared(prepared_scene_description prepared,
                                                    const asset_bindings& assets,
                                                    scene_instance_id& output) noexcept {
  output = {};
  if (!is_valid()) {
    return engine::error::invalid_argument;
  }
  try {
    const auto find_asset = [&](std::string_view uri) {
      const auto found = assets.find(uri);
      return found == assets.end() ? engine::render_asset_ref{} : found->second;
    };
    for (const auto& [uri, source] : prepared.prefabs) {
      if (!source.resource)
        return error::invalid_argument;
    }
    auto instance = std::make_unique<scene_instance>(native_world_, loader_, registry_);
    instance->initialize_staged(std::move(prepared.description));
    std::unordered_map<std::string, scene_node_id> nodes;
    nodes.reserve(instance->source().objects.size());
    for (const auto index : prepared.parent_first) {
      const auto& source = instance->source().objects.at(index);
      const auto result = instance->create_staged_node(
          index, source.parent_uuid ? nodes.at(*source.parent_uuid) : null_scene_node_id,
          asset_cast<mesh_resource>(source.mesh_renderer
                                        ? find_asset(source.mesh_renderer->mesh_uri)
                                        : engine::render_asset_ref{}),
          asset_cast<material_resource>(source.mesh_renderer
                                            ? find_asset(source.mesh_renderer->material_uri)
                                            : engine::render_asset_ref{}));
      if (result != engine::error::success) {
        return result;
      }
      nodes.emplace(source.uuid, instance->staged_node(index));
    }
    for (const auto& source : instance->source().prefab_instances) {
      const auto lease = prepared.prefabs.at(source.prefab_uri).resource;
      auto prefab = std::make_unique<prefab_runtime_instance>(native_world_, loader_, lease,
                                                              source.instance_uuid);
      math::transform transform{};
      transform.translation = source.translation;
      transform.rotation = source.rotation;
      transform.scale = source.scale;
      auto result = prefab->create_root(
          source.parent_uuid ? nodes.at(*source.parent_uuid) : null_scene_node_id, transform);
      if (result != engine::error::success) {
        return result;
      }
      result = native_world_.node_set_identity(prefab->root(), {source.instance_uuid, source.name});
      if (result.failed())
        return result;
      std::unordered_map<std::string, scene_node_id> prefab_nodes;
      prefab_nodes.reserve(lease.get()->objects.size());
      for (const auto index : prepared.prefabs.at(source.prefab_uri).parent_first) {
        const auto& node = lease.get()->objects.at(index);
        result = prefab->create_node(
            index, node.parent_uuid ? prefab_nodes.at(*node.parent_uuid) : prefab->root(),
            asset_cast<mesh_resource>(node.mesh_renderer ? find_asset(node.mesh_renderer->mesh_uri)
                                                         : engine::render_asset_ref{}),
            asset_cast<material_resource>(node.mesh_renderer
                                              ? find_asset(node.mesh_renderer->material_uri)
                                              : engine::render_asset_ref{}));
        if (result != engine::error::success) {
          return result;
        }
        prefab_runtime_instance::node_info info;
        result = prefab->get_node_info(index, info);
        if (result != engine::error::success) {
          return result;
        }
        prefab_nodes.emplace(node.uuid, info.node);
      }
      for (const auto& override_value : source.overrides) {
        result = prefab->apply_override(registry_, override_value);
        if (result != engine::error::success) {
          return result;
        }
      }
      instance->append_staged_prefab(std::move(prefab));
    }
    auto created = instances_.create(resource_type::scene_instance, std::move(instance));
    if (!created)
      return created.error();
    output = *created;
    return error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
error scene_instances::capture_prepared(scene_instance_id instance,
                                        prepared_scene_description& description,
                                        asset_bindings& assets) const noexcept try {
  const auto* value = instances_.get(instance, resource_type::scene_instance);
  if (!value || !*value)
    return error::invalid_handle;
  auto scene = std::make_shared<scene_description>();
  auto status = (*value)->capture_description(*scene);
  if (status.failed())
    return status;
  scene_description_inputs inputs;
  inputs.scene = asset_ref<const scene_description>{scene};
  for (const auto& prefab : scene->prefab_instances) {
    prefab_instance_description author;
    prefab_asset_ref source;
    status = (*value)->capture_prefab_instance_header(prefab.instance_uuid, author, source);
    if (status.failed())
      return status;
    inputs.prefabs.insert_or_assign(prefab.prefab_uri, std::move(source));
  }
  prepared_scene_description prepared;
  scene_diagnostic diagnostic;
  status = prepare_loaded_scene(inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  asset_bindings bindings;
  for (const auto& source : prepared.assets) {
    render_asset_ref resource;
    status = engine::error::success;
    {
      auto loaded = loader_.find(source);
      if (loaded)
        resource = std::move(*loaded);
      else {
        resource = {};
        status = loaded.error();
      }
    };
    if (status.failed())
      return status;
    bindings.emplace(source.uri, std::move(resource));
  }
  description = std::move(prepared);
  assets = std::move(bindings);
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

} // namespace gneiss::engine
