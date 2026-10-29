// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/core/error.hpp"
#include "engine/function/resource/asset_manager.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::engine {
namespace {

engine::error order_nodes(const std::vector<object_description>& objects,
                          std::vector<std::size_t>& order, std::size_t maximum_depth,
                          const std::function<bool()>& cancelled,
                          std::size_t* out_depth = nullptr) {
  if (out_depth != nullptr) {
    *out_depth = 0U;
  }
  std::unordered_map<guid, std::size_t, guid_hash> indices;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    if (!indices.emplace(objects[index].uuid, index).second) {
      return engine::error::invalid_argument;
    }
  }
  std::vector<std::vector<std::size_t>> children(objects.size());
  std::vector<std::size_t> depths(objects.size(), 1U);
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const auto& parent_uuid = objects[index].parent_uuid;
    if (!parent_uuid) {
      order.push_back(index);
    } else {
      const auto parent = indices.find(*parent_uuid);
      if (parent == indices.end()) {
        return engine::error::invalid_argument;
      }
      children[parent->second].push_back(index);
    }
  }
  for (std::size_t cursor = 0; cursor < order.size(); ++cursor) {
    if (cancelled && cancelled()) {
      return engine::error::invalid_state;
    }
    const auto index = order[cursor];
    if (depths[index] > maximum_depth) {
      return engine::error::invalid_argument;
    }
    if (out_depth != nullptr) {
      *out_depth = std::max(*out_depth, depths[index]);
    }
    for (const auto child : children[index]) {
      depths[child] = depths[index] + 1U;
      order.push_back(child);
    }
  }
  return order.size() == objects.size() ? engine::error::success : engine::error::invalid_argument;
}

class preparation final {
public:
  const scene_description_inputs& inputs;
  const std::function<bool()>& cancelled;
  scene_prepare_limits limits;
  prepared_scene_description candidate;
  std::map<std::string, engine::asset_type> dependencies;
  std::map<guid, std::size_t> scene_depths;

  engine::error collect(const std::vector<object_description>& objects) {
    using engine::asset_type;
    for (const auto& object : objects) {
      for (const auto& component : object.components) {
        if (!component.get<camera_description>() && !component.get<mesh_renderer_description>())
          return error::unsupported;
      }
      if (cancelled && cancelled()) {
        return engine::error::invalid_state;
      }
      if (!object.components.template get<mesh_renderer_description>()) {
        continue;
      }
      for (const auto& asset : {
               engine::asset_source{
                   .uri = object.components.template get<mesh_renderer_description>()->mesh_uri,
                   .type = asset_type::mesh,
               },
               engine::asset_source{
                   .uri = object.components.template get<mesh_renderer_description>()->material_uri,
                   .type = asset_type::material,
               },
           }) {
        const auto [entry, added] = dependencies.emplace(asset.uri, asset.type);
        if ((!added && entry->second != asset.type) || dependencies.size() > limits.dependencies) {
          return engine::error::invalid_argument;
        }
      }
    }
    return engine::error::success;
  }

  engine::error add_prefab(const object_description& instance) {
    auto result = engine::error::success;
    auto found = candidate.prefabs.find(instance.prefab->uri);
    if (found == candidate.prefabs.end()) {
      if (candidate.prefabs.size() == limits.dependencies) {
        return engine::error::invalid_argument;
      }
      const auto input = inputs.prefabs.find(instance.prefab->uri);
      if (input == inputs.prefabs.end() || !input->second)
        return error::not_found;
      prepared_prefab_description prefab;
      prefab.resource = input->second;
      const auto bytes = prefab.resource->author_json.size();
      if (bytes > limits.source_bytes - candidate.source_bytes)
        return error::invalid_argument;
      candidate.source_bytes += bytes;
      if (result == engine::error::success && prefab.resource->objects.size() > limits.nodes) {
        return engine::error::invalid_argument;
      }
      if (result == engine::error::success) {
        result = order_nodes(prefab.resource->objects, prefab.parent_first, limits.hierarchy_depth,
                             cancelled, &prefab.maximum_depth);
      }
      if (result == engine::error::success) {
        result = collect(prefab.resource->objects);
      }
      if (result != engine::error::success) {
        return result;
      }
      found = candidate.prefabs.emplace(instance.prefab->uri, std::move(prefab)).first;
    }
    const auto parent_depth = instance.parent_uuid ? scene_depths.at(*instance.parent_uuid) : 0U;
    if (parent_depth >= limits.hierarchy_depth ||
        found->second.maximum_depth > limits.hierarchy_depth - parent_depth) {
      return engine::error::invalid_argument;
    }
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
    candidate.source_bytes = candidate.description.author_json.size();
    if (candidate.source_bytes > limits.source_bytes)
      return error::invalid_argument;
    auto result = engine::error::success;
    candidate.instance_nodes = candidate.description.objects.size();
    if (candidate.instance_nodes > limits.nodes) {
      return engine::error::invalid_argument;
    }
    result = order_nodes(candidate.description.objects, candidate.parent_first,
                         limits.hierarchy_depth, cancelled);
    if (result == engine::error::success) {
      result = collect(candidate.description.objects);
    }
    if (result != engine::error::success) {
      return result;
    }
    for (const auto index : candidate.parent_first) {
      const auto& object = candidate.description.objects[index];
      scene_depths.emplace(object.uuid,
                           object.parent_uuid ? scene_depths.at(*object.parent_uuid) + 1U : 1U);
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
    for (const auto& [asset_uri, type] : dependencies) {
      candidate.assets.push_back({.uri = asset_uri, .type = type});
    }
    return engine::error::success;
  }
};

} // namespace

error prepare_loaded_scene(const scene_description_inputs& inputs, std::string_view prefab_uri,
                           prepared_scene_description& output, scene_diagnostic& diagnostic,
                           const std::function<bool()>& cancelled,
                           scene_prepare_limits limits) noexcept try {
  output = {};
  diagnostic = {};
  preparation work{.inputs = inputs,
                   .cancelled = cancelled,
                   .limits = limits,
                   .candidate = {},
                   .dependencies = {},
                   .scene_depths = {}};
  auto status = error::success;
  if (cancelled && cancelled())
    status = error::invalid_state;
  else if (prefab_uri.empty())
    status = work.run();
  else {
    object_description instance;
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

struct scene_description_request::state {
  asset_manager& assets;
  std::string uri;
  bool prefab{}, started{};
  scene_prepare_limits limits;
  asset_request<scene_description> scene;
  asset_request<prefab_description> current;
  scene_description_inputs inputs;
  std::vector<std::string> uris;
  std::size_t cursor{}, bytes{};
};
scene_description_request::scene_description_request(asset_manager& assets, std::string uri,
                                                     bool prefab, scene_prepare_limits limits)
    : state_(std::make_unique<state>(state{.assets = assets,
                                           .uri = std::move(uri),
                                           .prefab = prefab,
                                           .limits = limits,
                                           .scene = {},
                                           .current = {},
                                           .inputs = {},
                                           .uris = {}})) {}
scene_description_request::~scene_description_request() = default;
error scene_description_request::advance(scene_description_inputs& output,
                                         scene_diagnostic& diagnostic) {
  auto& s = *state_;
  const auto fail = [&](error status) {
    diagnostic = {status, 0, {}, {}};
    return status;
  };
  if (!s.started) {
    if (s.prefab) {
      s.uris.push_back(s.uri);
    } else {
      auto loaded = s.assets.load<scene_description>(s.uri, true);
      if (!loaded)
        return fail(loaded.error());
      s.scene = std::move(*loaded);
    }
    s.started = true;
  }
  if (s.scene) {
    auto loaded = s.scene.take();
    if (!loaded) {
      auto failure = std::move(loaded).error();
      diagnostic = {failure.error, failure.byte_offset, std::move(failure.path),
                    std::move(failure.message)};
      return diagnostic.error;
    }
    s.inputs.scene = std::move(*loaded);
    s.bytes = s.inputs.scene->author_json.size();
    for (const auto& instance : s.inputs.scene->objects)
      if (instance.prefab && std::ranges::find(s.uris, instance.prefab->uri) == s.uris.end())
        s.uris.push_back(instance.prefab->uri);
  }
  if (s.bytes > s.limits.source_bytes || s.uris.size() > s.limits.dependencies)
    return fail(error::invalid_argument);
  if (s.cursor < s.uris.size()) {
    const auto& uri = s.uris[s.cursor];
    if (!s.current) {
      auto loaded = s.assets.load<prefab_description>(uri, true);
      if (!loaded)
        return fail(loaded.error());
      s.current = std::move(*loaded);
    }
    auto loaded = s.current.take();
    if (!loaded) {
      auto failure = std::move(loaded).error();
      diagnostic = {failure.error, failure.byte_offset, std::move(failure.path),
                    std::move(failure.message)};
      return diagnostic.error;
    }
    auto value = std::move(*loaded);
    if (value->author_json.size() > s.limits.source_bytes - s.bytes)
      return fail(error::invalid_argument);
    s.bytes += value->author_json.size();
    s.inputs.prefabs.emplace(uri, std::move(value));
    ++s.cursor;
    if (s.cursor < s.uris.size())
      return error::not_ready;
  }
  output = std::move(s.inputs);
  diagnostic = {};
  return error::success;
}
} // namespace gneiss::engine
