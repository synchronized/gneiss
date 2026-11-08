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
error scene_instances::replace_prepared(scene_instance_id instance,
                                        scene_instantiation_data prepared,
                                        const resource_bindings& assets) noexcept {
  if (!native_world_.is_owner_thread())
    return error::invalid_state;
  if (!instances_.get(instance, resource_type::scene_instance))
    return error::invalid_handle;
  auto created = instantiate_prepared(std::move(prepared), assets);
  if (!created)
    return created.error();
  // 插入可能扩容 RID 表，必须重新获取槽位地址。
  auto* target = instances_.get(instance, resource_type::scene_instance);
  auto* replacement = instances_.get(*created, resource_type::scene_instance);
  std::swap(*target, *replacement);
  return unload(*created);
}
result<scene_instantiation_result, scene_diagnostic>
scene_instances::instantiate(ref<const scene> source, std::size_t source_bytes) noexcept try {
  if (!native_world_.is_owner_thread())
    return scene_diagnostic{error::invalid_state, 0, {}, {}};
  if (!is_valid())
    return scene_diagnostic{error::invalid_argument, 0, {}, {}};
  auto prepared = make_instantiation_data(std::move(source), source_bytes);
  if (!prepared)
    return std::move(prepared).error();
  resource_bindings bindings;
  for (const auto& request : prepared->assets) {
    auto resource = loader_.load(request);
    if (!resource)
      return scene_diagnostic{resource.error(), 0, request.uri, "无法提交场景视觉加载"};
    bindings.emplace(request.uri, resource->resource());
  }
  scene_instantiation_result output{.instance = {},
                                    .node_count = prepared->instance_nodes,
                                    .assets = std::move(prepared->assets),
                                    .bindings = {}};
  auto instance = instantiate_prepared(std::move(*prepared), bindings);
  if (!instance)
    return scene_diagnostic{instance.error(), 0, {}, {}};
  output.instance = *instance;
  output.bindings = std::move(bindings);
  return output;
} catch (const std::bad_alloc&) {
  return scene_diagnostic{error::out_of_memory, 0, {}, {}};
} catch (...) {
  return scene_diagnostic{error::internal, 0, {}, {}};
}

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
            index, source, source.parent_uuid ? nodes.at(source.parent_uuid) : null_scene_node_id,
            renderer.mesh, renderer.material);
        if (result != engine::error::success) {
          return result;
        }
        nodes.emplace(source.uuid, instance->staged_node(index));
        continue;
      }
      const auto lease = prepared.prefabs.at(source.prefab->uri).resource;
      auto replayed = replay_prefab_reference(*lease, *source.prefab);
      if (!replayed)
        return replayed.error();
      const auto& effective = replayed->description;
      auto prefab =
          std::make_unique<prefab_binding>(native_world_, loader_, lease, source.uuid,
                                           prepared.prefabs.at(source.prefab->uri).source_asset);
      auto result = prefab->create_root(
          source.parent_uuid ? nodes.at(source.parent_uuid) : null_scene_node_id, source.transform);
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
        result = prefab->create_node(
            node_index, node, node.parent_uuid ? prefab_nodes.at(node.parent_uuid) : prefab->root(),
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
} // namespace gneiss::engine
