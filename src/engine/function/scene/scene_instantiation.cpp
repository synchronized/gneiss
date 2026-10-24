// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::engine::scene {
gneiss::engine::core::result
scene_instances::instantiate_prepared(prepared_scene_description prepared,
                                      const asset_bindings& assets,
                                      gneiss_scene_instance& output) noexcept {
  output = GNEISS_NULL_SCENE_INSTANCE;
  if (!is_valid()) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    const auto find_asset = [&](std::string_view uri) {
      const auto found = assets.find(uri);
      return found == assets.end() ? engine::render::render_asset_lease{} : found->second;
    };
    std::map<std::string, engine::resource::prefab_asset_lease> prefabs;
    for (auto& [uri, source] : prepared.prefabs) {
      engine::resource::prefab_asset_lease lease;
      const auto result =
          prefab_loader_.install_prepared(uri, std::move(source.description), lease);
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
      prefabs.emplace(uri, std::move(lease));
    }
    auto instance = std::make_unique<scene_instance>(native_world_, reflection_context_, loader_,
                                                     prefab_loader_, registry_);
    instance->initialize_staged(std::move(prepared.description));
    std::unordered_map<std::string, gneiss_scene_node_id> nodes;
    nodes.reserve(instance->source().objects.size());
    for (const auto index : prepared.parent_first) {
      const auto& source = instance->source().objects.at(index);
      const auto result = instance->create_staged_node(
          index, source.parent_uuid ? nodes.at(*source.parent_uuid) : GNEISS_NULL_SCENE_NODE_ID,
          engine::render::render_asset_loader::mesh_lease(
              source.mesh_renderer ? find_asset(source.mesh_renderer->mesh_uri)
                                   : engine::render::render_asset_lease{}),
          engine::render::render_asset_loader::material_lease(
              source.mesh_renderer ? find_asset(source.mesh_renderer->material_uri)
                                   : engine::render::render_asset_lease{}));
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
      nodes.emplace(source.uuid, instance->staged_node(index));
    }
    for (const auto& source : instance->source().prefab_instances) {
      const auto lease = prefabs.at(source.prefab_uri);
      auto prefab = std::make_unique<prefab_runtime_instance>(native_world_, reflection_context_,
                                                              loader_, lease, source.instance_uuid);
      engine::core::math::transform transform{};
      std::ranges::copy(source.translation, transform.translation.begin());
      std::ranges::copy(source.rotation, transform.rotation.begin());
      std::ranges::copy(source.scale, transform.scale.begin());
      auto result = prefab->begin_staged(source.parent_uuid ? nodes.at(*source.parent_uuid)
                                                            : GNEISS_NULL_SCENE_NODE_ID,
                                         transform);
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
      std::unordered_map<std::string, gneiss_scene_node_id> prefab_nodes;
      prefab_nodes.reserve(lease.get()->objects.size());
      for (const auto index : prepared.prefabs.at(source.prefab_uri).parent_first) {
        const auto& node = lease.get()->objects.at(index);
        result = prefab->create_staged_node(
            index, node.parent_uuid ? prefab_nodes.at(*node.parent_uuid) : prefab->root(),
            engine::render::render_asset_loader::mesh_lease(
                node.mesh_renderer ? find_asset(node.mesh_renderer->mesh_uri)
                                   : engine::render::render_asset_lease{}),
            engine::render::render_asset_loader::material_lease(
                node.mesh_renderer ? find_asset(node.mesh_renderer->material_uri)
                                   : engine::render::render_asset_lease{}));
        if (result != gneiss::engine::core::result::success) {
          return result;
        }
        prefab_runtime_instance::node_info info;
        result = prefab->get_node_info(index, info);
        if (result != gneiss::engine::core::result::success) {
          return result;
        }
        prefab_nodes.emplace(node.uuid, info.node);
      }
      for (const auto& override_value : source.overrides) {
        result = prefab->apply_staged_override(registry_, override_value);
        if (result != gneiss::engine::core::result::success) {
          return result;
        }
      }
      instance->append_staged_prefab(std::move(prefab));
    }
    return instances_.create(engine::core::resource_type::scene_instance, std::move(instance),
                             &output);
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}
} // namespace gneiss::engine::scene
