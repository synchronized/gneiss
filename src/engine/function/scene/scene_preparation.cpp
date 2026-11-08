// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_preparation.hpp"
#include "engine/core/error.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/prefab_replay.hpp"
#include "engine/function/scene/scene_composition.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"

#include <algorithm>

namespace gneiss::engine {
namespace {

engine::error validate_hierarchy(const std::vector<game_object_description>& objects,
                                 std::size_t maximum_depth, const std::function<bool()>& cancelled,
                                 scene_diagnostic& diagnostic,
                                 std::optional<guid> root = std::nullopt) {
  auto hierarchy = inspect_object_hierarchy(objects, root, maximum_depth, cancelled);
  if (!hierarchy) {
    diagnostic = std::move(hierarchy).error();
    return diagnostic.error;
  }
  return error::success;
}

class preparation final {
public:
  const scene_preparation_inputs& inputs;
  scene_diagnostic* diagnostic{};
  const std::function<bool()>& cancelled;
  scene_prepare_limits limits;
  scene_instantiation_data candidate;
  std::map<std::string, engine::render_resource_kind> dependencies;

  engine::error collect(const std::vector<game_object_description>& objects) {
    using engine::render_resource_kind;
    for (const auto& object : objects) {
      if (cancelled && cancelled())
        return error::invalid_state;
      auto sources = component_dependencies(object.components);
      if (!sources)
        return sources.error();
      for (const auto& asset : *sources) {
        const auto [entry, added] = dependencies.emplace(asset.uri, asset.type);
        if ((!added && entry->second != asset.type) || dependencies.size() > limits.dependencies) {
          return engine::error::invalid_argument;
        }
      }
    }
    return engine::error::success;
  }

  engine::error add_prefab(const game_object_description& instance) {
    auto result = engine::error::success;
    auto found = candidate.prefabs.find(instance.prefab->uri);
    if (found == candidate.prefabs.end()) {
      if (candidate.prefabs.size() == limits.dependencies) {
        return engine::error::invalid_argument;
      }
      const auto input = inputs.prefabs.find(instance.prefab->uri);
      if (input == inputs.prefabs.end() || !input->second)
        return error::not_found;
      prefab_instantiation_data prefab;
      prefab.resource = input->second;
      if (auto asset = inputs.prefab_assets.find(instance.prefab->uri);
          asset != inputs.prefab_assets.end())
        prefab.source_asset = asset->second;
      if (result == engine::error::success && prefab.resource->objects.size() > limits.nodes) {
        return engine::error::invalid_argument;
      }
      if (result == engine::error::success) {
        result = validate_hierarchy(prefab.resource->objects, limits.hierarchy_depth, cancelled,
                                    *diagnostic, prefab.resource->root_uuid);
      }
      if (result != engine::error::success) {
        return result;
      }
      found = candidate.prefabs.emplace(instance.prefab->uri, std::move(prefab)).first;
    }
    if (!instance.prefab->overrides.empty() || !instance.prefab->component_overrides.empty()) {
      auto replayed = replay_prefab_reference(*found->second.resource, *instance.prefab);
      if (!replayed)
        return replayed.error();
      result = collect(replayed->description.objects);
    } else {
      result = collect(found->second.resource->objects);
    }
    if (result.failed())
      return result;
    // 来源根直接作为实例根，共享描述不等于共享实例。
    const auto count = found->second.resource->objects.size();
    if (count > limits.nodes - candidate.instance_nodes) {
      return engine::error::invalid_argument;
    }
    candidate.instance_nodes += count;
    return engine::error::success;
  }

  engine::error run() {
    if (!inputs.scene)
      return error::invalid_argument;
    candidate.description = *inputs.scene;
    auto result = engine::error::success;
    candidate.instance_nodes = candidate.description.objects.size();
    if (candidate.instance_nodes > limits.nodes) {
      return engine::error::invalid_argument;
    }
    result = validate_hierarchy(candidate.description.objects, limits.hierarchy_depth, cancelled,
                                *diagnostic);
    if (result == engine::error::success) {
      result = collect(candidate.description.objects);
    }
    if (result != engine::error::success) {
      return result;
    }
    for (const auto& instance : candidate.description.objects) {
      if (!instance.prefab)
        continue;
      --candidate.instance_nodes;
      result = add_prefab(instance);
      if (result != engine::error::success) {
        return result;
      }
    }
    return finish();
  }

  engine::error finish() {
    if (cancelled && cancelled()) {
      return engine::error::invalid_state;
    }
    std::map<std::string, const prefab_description*, std::less<>> sources;
    for (const auto& [uri, prefab] : candidate.prefabs)
      sources.emplace(uri, prefab.resource.get());
    auto composed = compose_scene(candidate.description, sources);
    if (!composed)
      return composed.error();
    std::map<object_address, object_address> parents;
    for (const auto& object : composed->objects)
      parents.emplace(object.address, object.parent);
    for (const auto& object : composed->objects) {
      std::size_t depth = 1;
      auto parent = object.parent;
      while (parent.object) {
        ++depth;
        parent = parents.at(parent);
      }
      if (depth > limits.hierarchy_depth)
        return error::invalid_argument;
    }
    for (const auto& [asset_uri, type] : dependencies) {
      candidate.assets.push_back({.uri = asset_uri, .type = type});
    }
    return engine::error::success;
  }
};

} // namespace

error prepare_loaded_scene(const scene_preparation_inputs& inputs, std::string_view prefab_uri,
                           scene_instantiation_data& output, scene_diagnostic& diagnostic,
                           const std::function<bool()>& cancelled,
                           scene_prepare_limits limits) noexcept try {
  output = {};
  diagnostic = {};
  preparation work{.inputs = inputs,
                   .diagnostic = &diagnostic,
                   .cancelled = cancelled,
                   .limits = limits,
                   .candidate = {},
                   .dependencies = {}};
  work.candidate.source_bytes = inputs.source_bytes;
  work.candidate.source_asset = inputs.source_asset;
  auto status = error::success;
  if (inputs.source_bytes > limits.source_bytes)
    status = error::invalid_argument;
  else if (cancelled && cancelled())
    status = error::invalid_state;
  else if (prefab_uri.empty())
    status = work.run();
  else {
    game_object_description instance;
    instance.prefab.emplace();
    instance.prefab->uri = prefab_uri;
    status = work.add_prefab(instance);
    if (status == error::success)
      status = work.finish();
  }
  if (status == error::success)
    output = std::move(work.candidate);
  diagnostic.error = status;
  return status;
} catch (const std::bad_alloc&) {
  diagnostic.error = error::out_of_memory;
  return diagnostic.error;
} catch (...) {
  diagnostic.error = error::internal;
  return diagnostic.error;
}

result<scene_instantiation_data, scene_diagnostic>
make_instantiation_data(ref<const scene> source, std::size_t source_bytes,
                        scene_prepare_limits limits) noexcept try {
  if (!source)
    return scene_diagnostic{error::invalid_argument, 0, {}, {}};
  auto content = source->content();
  scene_preparation_inputs inputs;
  inputs.scene = content->description;
  inputs.source_asset = std::move(source);
  inputs.source_bytes = source_bytes;
  for (const auto& [uri, dependency] : content->prefabs) {
    if (inputs.source_bytes > limits.source_bytes ||
        dependency.source_bytes > limits.source_bytes - inputs.source_bytes)
      return scene_diagnostic{error::invalid_argument, 0, {}, {}};
    inputs.source_bytes += dependency.source_bytes;
    inputs.prefabs.emplace(uri, dependency.description);
    inputs.prefab_assets.emplace(uri, dependency.resource);
  }
  scene_instantiation_data output;
  scene_diagnostic diagnostic;
  auto status = prepare_loaded_scene(inputs, {}, output, diagnostic, {}, limits);
  if (status.failed())
    return diagnostic;
  return output;
} catch (const std::bad_alloc&) {
  return scene_diagnostic{error::out_of_memory, 0, {}, {}};
} catch (...) {
  return scene_diagnostic{error::internal, 0, {}, {}};
}
result<scene_instantiation_data, scene_diagnostic>
make_instantiation_data(ref<const prefab> source, std::size_t source_bytes,
                        scene_prepare_limits limits) noexcept try {
  if (!source)
    return scene_diagnostic{error::invalid_argument, 0, {}, {}};
  const auto& uri = source->source().uri;
  scene_preparation_inputs inputs;
  inputs.source_bytes = source_bytes;
  inputs.prefabs.emplace(uri, source->snapshot());
  inputs.prefab_assets.emplace(uri, source);
  scene_instantiation_data output;
  scene_diagnostic diagnostic;
  auto status = prepare_loaded_scene(inputs, uri, output, diagnostic, {}, limits);
  if (status.failed())
    return diagnostic;
  return output;
} catch (const std::bad_alloc&) {
  return scene_diagnostic{error::out_of_memory, 0, {}, {}};
} catch (...) {
  return scene_diagnostic{error::internal, 0, {}, {}};
}

} // namespace gneiss::engine
