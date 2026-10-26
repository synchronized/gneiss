// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::engine {
gneiss::engine::result scene_instances::instantiate_prepared(prepared_scene_description prepared,
                                                             const asset_bindings& assets,
                                                             scene_instance_id& output) noexcept {
  output = {};
  if (!is_valid()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    const auto find_asset = [&](std::string_view uri) {
      const auto found = assets.find(uri);
      return found == assets.end() ? gneiss::engine::render_asset_ref{} : found->second;
    };
    for (const auto& [uri, source] : prepared.prefabs) {
      if (!source.resource)
        return result::invalid_argument;
    }
    auto instance = std::make_unique<scene_instance>(native_world_, loader_, registry_);
    instance->initialize_staged(std::move(prepared.description));
    std::unordered_map<std::string, scene_node_id> nodes;
    nodes.reserve(instance->source().objects.size());
    for (const auto index : prepared.parent_first) {
      const auto& source = instance->source().objects.at(index);
      const auto result = instance->create_staged_node(
          index, source.parent_uuid ? nodes.at(*source.parent_uuid) : null_scene_node_id,
          gneiss::engine::render_asset_loader::mesh_ref(
              source.mesh_renderer ? find_asset(source.mesh_renderer->mesh_uri)
                                   : gneiss::engine::render_asset_ref{}),
          gneiss::engine::render_asset_loader::material_ref(
              source.mesh_renderer ? find_asset(source.mesh_renderer->material_uri)
                                   : gneiss::engine::render_asset_ref{}));
      if (result != gneiss::engine::result::success) {
        return result;
      }
      nodes.emplace(source.uuid, instance->staged_node(index));
    }
    for (const auto& source : instance->source().prefab_instances) {
      const auto lease = prepared.prefabs.at(source.prefab_uri).resource;
      auto prefab = std::make_unique<prefab_runtime_instance>(native_world_, loader_, lease,
                                                              source.instance_uuid);
      gneiss::engine::transform transform{};
      transform.translation = source.translation;
      transform.rotation = source.rotation;
      transform.scale = source.scale;
      auto result = prefab->create_root(
          source.parent_uuid ? nodes.at(*source.parent_uuid) : null_scene_node_id, transform);
      if (result != gneiss::engine::result::success) {
        return result;
      }
      std::unordered_map<std::string, scene_node_id> prefab_nodes;
      prefab_nodes.reserve(lease.get()->objects.size());
      for (const auto index : prepared.prefabs.at(source.prefab_uri).parent_first) {
        const auto& node = lease.get()->objects.at(index);
        result = prefab->create_node(
            index, node.parent_uuid ? prefab_nodes.at(*node.parent_uuid) : prefab->root(),
            gneiss::engine::render_asset_loader::mesh_ref(
                node.mesh_renderer ? find_asset(node.mesh_renderer->mesh_uri)
                                   : gneiss::engine::render_asset_ref{}),
            gneiss::engine::render_asset_loader::material_ref(
                node.mesh_renderer ? find_asset(node.mesh_renderer->material_uri)
                                   : gneiss::engine::render_asset_ref{}));
        if (result != gneiss::engine::result::success) {
          return result;
        }
        prefab_runtime_instance::node_info info;
        result = prefab->get_node_info(index, info);
        if (result != gneiss::engine::result::success) {
          return result;
        }
        prefab_nodes.emplace(node.uuid, info.node);
      }
      for (const auto& override_value : source.overrides) {
        result = prefab->apply_override(registry_, override_value);
        if (result != gneiss::engine::result::success) {
          return result;
        }
      }
      instance->append_staged_prefab(std::move(prefab));
    }
    return instances_.create(gneiss::engine::resource_type::scene_instance, std::move(instance),
                             &output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
} // namespace gneiss::engine
