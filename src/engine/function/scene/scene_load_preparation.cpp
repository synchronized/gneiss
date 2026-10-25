// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/core/result.hpp"
#include "engine/resource/asset_manager.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::engine {
namespace {

gneiss::engine::result order_nodes(const std::vector<gneiss::engine::object_description>& objects,
                                   std::vector<std::size_t>& order, std::size_t maximum_depth,
                                   const std::function<bool()>& cancelled,
                                   std::size_t* out_depth = nullptr) {
  if (out_depth != nullptr) {
    *out_depth = 0U;
  }
  std::unordered_map<std::string_view, std::size_t> indices;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    if (!indices.emplace(objects[index].uuid, index).second) {
      return gneiss::engine::result::invalid_argument;
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
        return gneiss::engine::result::invalid_argument;
      }
      children[parent->second].push_back(index);
    }
  }
  for (std::size_t cursor = 0; cursor < order.size(); ++cursor) {
    if (cancelled && cancelled()) {
      return gneiss::engine::result::invalid_state;
    }
    const auto index = order[cursor];
    if (depths[index] > maximum_depth) {
      return gneiss::engine::result::invalid_argument;
    }
    if (out_depth != nullptr) {
      *out_depth = std::max(*out_depth, depths[index]);
    }
    for (const auto child : children[index]) {
      depths[child] = depths[index] + 1U;
      order.push_back(child);
    }
  }
  return order.size() == objects.size() ? gneiss::engine::result::success
                                        : gneiss::engine::result::invalid_argument;
}

class preparation final {
public:
  const scene_description_inputs& inputs;
  const std::function<bool()>& cancelled;
  scene_prepare_limits limits;
  prepared_scene_description candidate;
  std::map<std::string, gneiss::engine::render_asset_type> dependencies;
  std::map<std::string, std::size_t> scene_depths;

  gneiss::engine::result collect(const std::vector<gneiss::engine::object_description>& objects) {
    using gneiss::engine::render_asset_type;
    for (const auto& object : objects) {
      if (cancelled && cancelled()) {
        return gneiss::engine::result::invalid_state;
      }
      if (!object.mesh_renderer) {
        continue;
      }
      for (const auto& asset : {
               gneiss::engine::render_asset_reload{
                   .uri = object.mesh_renderer->mesh_uri,
                   .type = render_asset_type::mesh,
               },
               gneiss::engine::render_asset_reload{
                   .uri = object.mesh_renderer->material_uri,
                   .type = render_asset_type::material,
               },
           }) {
        const auto [entry, added] = dependencies.emplace(asset.uri, asset.type);
        if ((!added && entry->second != asset.type) || dependencies.size() > limits.dependencies) {
          return gneiss::engine::result::invalid_argument;
        }
      }
    }
    return gneiss::engine::result::success;
  }

  gneiss::engine::result add_prefab(const gneiss::engine::prefab_instance_description& instance) {
    auto result = gneiss::engine::result::success;
    auto found = candidate.prefabs.find(instance.prefab_uri);
    if (found == candidate.prefabs.end()) {
      if (candidate.prefabs.size() == limits.dependencies) {
        return gneiss::engine::result::invalid_argument;
      }
      const auto input = inputs.prefabs.find(instance.prefab_uri);
      if (input == inputs.prefabs.end() || !input->second)
        return result::not_found;
      prepared_prefab_description prefab;
      prefab.resource = input->second;
      const auto bytes = prefab.resource->author_json.size();
      if (bytes > limits.source_bytes - candidate.source_bytes)
        return result::invalid_argument;
      candidate.source_bytes += bytes;
      if (result == gneiss::engine::result::success &&
          prefab.resource->objects.size() > limits.nodes) {
        return gneiss::engine::result::invalid_argument;
      }
      if (result == gneiss::engine::result::success) {
        result = order_nodes(prefab.resource->objects, prefab.parent_first, limits.hierarchy_depth,
                             cancelled, &prefab.maximum_depth);
      }
      if (result == gneiss::engine::result::success) {
        result = collect(prefab.resource->objects);
      }
      if (result != gneiss::engine::result::success) {
        return result;
      }
      found = candidate.prefabs.emplace(instance.prefab_uri, std::move(prefab)).first;
    }
    const auto parent_depth = instance.parent_uuid ? scene_depths.at(*instance.parent_uuid) : 0U;
    if (parent_depth >= limits.hierarchy_depth ||
        found->second.maximum_depth > limits.hierarchy_depth - parent_depth - 1U) {
      return gneiss::engine::result::invalid_argument;
    }
    // 每个 Prefab 实例还有一个容器节点，共享描述不等于共享实例。
    const auto count = found->second.resource->objects.size();
    if (count >= limits.nodes - candidate.instance_nodes) {
      return gneiss::engine::result::invalid_argument;
    }
    candidate.instance_nodes += count + 1U;
    return gneiss::engine::result::success;
  }

  gneiss::engine::result run() {
    if (!inputs.scene)
      return result::invalid_argument;
    candidate.description = *inputs.scene;
    candidate.source_bytes = candidate.description.author_json.size();
    if (candidate.source_bytes > limits.source_bytes)
      return result::invalid_argument;
    auto result = gneiss::engine::result::success;
    candidate.instance_nodes = candidate.description.objects.size();
    if (candidate.instance_nodes > limits.nodes) {
      return gneiss::engine::result::invalid_argument;
    }
    result = order_nodes(candidate.description.objects, candidate.parent_first,
                         limits.hierarchy_depth, cancelled);
    if (result == gneiss::engine::result::success) {
      result = collect(candidate.description.objects);
    }
    if (result != gneiss::engine::result::success) {
      return result;
    }
    for (const auto index : candidate.parent_first) {
      const auto& object = candidate.description.objects[index];
      scene_depths.emplace(object.uuid,
                           object.parent_uuid ? scene_depths.at(*object.parent_uuid) + 1U : 1U);
    }
    for (const auto& instance : candidate.description.prefab_instances) {
      result = add_prefab(instance);
      if (result != gneiss::engine::result::success) {
        return result;
      }
    }
    return finish();
  }

  gneiss::engine::result finish() {
    if (cancelled && cancelled()) {
      return gneiss::engine::result::invalid_state;
    }
    for (const auto& [asset_uri, type] : dependencies) {
      candidate.assets.push_back({.uri = asset_uri, .type = type});
    }
    return gneiss::engine::result::success;
  }
};

} // namespace

result prepare_loaded_scene(const scene_description_inputs& inputs, std::string_view prefab_uri,
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
  auto status = result::success;
  if (cancelled && cancelled())
    status = result::invalid_state;
  else if (prefab_uri.empty())
    status = work.run();
  else {
    prefab_instance_description instance;
    instance.prefab_uri = prefab_uri;
    status = work.add_prefab(instance);
    if (status == result::success)
      status = work.finish();
  }
  if (status == result::success)
    output = std::move(work.candidate);
  diagnostic.result = status;
  return status;
} catch (const std::bad_alloc&) {
  diagnostic.result = result::out_of_memory;
  return diagnostic.result;
} catch (...) {
  diagnostic.result = result::internal;
  return diagnostic.result;
}

namespace {
result prepare_sync(asset_manager& assets, std::string_view uri, bool prefab,
                    prepared_scene_description& output, scene_diagnostic& diagnostic,
                    const std::function<bool()>& cancelled, scene_prepare_limits limits) noexcept
    try {
  output = {};
  diagnostic = {};
  scene_description_inputs inputs;
  serialization_diagnostic decoded;
  auto status = result::success;
  if (cancelled && cancelled())
    return result::invalid_state;
  if (!prefab)
    status = assets.load_sync(uri, inputs.scene, true, &decoded);
  std::vector<std::string> uris;
  if (prefab)
    uris.emplace_back(uri);
  else if (status == result::success)
    for (const auto& instance : inputs.scene->prefab_instances)
      if (std::ranges::find(uris, instance.prefab_uri) == uris.end())
        uris.push_back(instance.prefab_uri);
  if (uris.size() > limits.dependencies)
    return result::invalid_argument;
  std::size_t bytes = inputs.scene ? inputs.scene->author_json.size() : 0U;
  for (const auto& source : uris) {
    if (status.failed())
      break;
    if (cancelled && cancelled())
      return result::invalid_state;
    if (bytes > limits.source_bytes)
      return result::invalid_argument;
    asset_ref<const prefab_description> value;
    status = assets.load_sync(source, value, true, &decoded);
    if (status == result::success) {
      if (value->author_json.size() > limits.source_bytes - bytes)
        return result::invalid_argument;
      bytes += value->author_json.size();
      inputs.prefabs.emplace(source, std::move(value));
    }
  }
  if (status.failed()) {
    diagnostic = {status, decoded.byte_offset, std::move(decoded.path), std::move(decoded.message)};
    return status;
  }
  return prepare_loaded_scene(inputs, prefab ? uri : std::string_view{}, output, diagnostic,
                              cancelled, limits);
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
}
result prepare_scene_description(asset_manager& assets, std::string_view uri,
                                 prepared_scene_description& output, scene_diagnostic& diagnostic,
                                 const std::function<bool()>& cancelled,
                                 scene_prepare_limits limits) noexcept {
  const auto status = prepare_sync(assets, uri, false, output, diagnostic, cancelled, limits);
  diagnostic.result = status;
  return status;
}
result prepare_prefab_description(asset_manager& assets, std::string_view uri,
                                  prepared_scene_description& output, scene_diagnostic& diagnostic,
                                  const std::function<bool()>& cancelled,
                                  scene_prepare_limits limits) noexcept {
  const auto status = prepare_sync(assets, uri, true, output, diagnostic, cancelled, limits);
  diagnostic.result = status;
  return status;
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
result scene_description_request::advance(scene_description_inputs& output,
                                          scene_diagnostic& diagnostic) {
  auto& s = *state_;
  serialization_diagnostic decoded;
  const auto fail = [&](result status) {
    diagnostic = {status, decoded.byte_offset, std::move(decoded.path), std::move(decoded.message)};
    return status;
  };
  s.assets.advance_resources();
  if (!s.started) {
    auto status = result::success;
    if (s.prefab)
      s.uris.push_back(s.uri);
    else
      status = s.assets.load(s.uri, s.scene, true);
    if (status.failed())
      return fail(status);
    s.started = true;
  }
  if (s.scene) {
    const auto status = s.scene.take(s.inputs.scene, &decoded);
    if (status != result::success)
      return fail(status);
    s.bytes = s.inputs.scene->author_json.size();
    for (const auto& instance : s.inputs.scene->prefab_instances)
      if (std::ranges::find(s.uris, instance.prefab_uri) == s.uris.end())
        s.uris.push_back(instance.prefab_uri);
  }
  if (s.bytes > s.limits.source_bytes || s.uris.size() > s.limits.dependencies)
    return fail(result::invalid_argument);
  if (s.cursor < s.uris.size()) {
    const auto& uri = s.uris[s.cursor];
    if (!s.current) {
      const auto status = s.assets.load(uri, s.current, true);
      if (status.failed())
        return fail(status);
    }
    asset_ref<const prefab_description> value;
    const auto status = s.current.take(value, &decoded);
    if (status != result::success)
      return fail(status);
    if (value->author_json.size() > s.limits.source_bytes - s.bytes)
      return fail(result::invalid_argument);
    s.bytes += value->author_json.size();
    s.inputs.prefabs.emplace(uri, std::move(value));
    ++s.cursor;
    if (s.cursor < s.uris.size())
      return result::not_ready;
  }
  output = std::move(s.inputs);
  diagnostic = {};
  return result::success;
}
} // namespace gneiss::engine
