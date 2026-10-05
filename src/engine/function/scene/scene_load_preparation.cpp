// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_load_preparation.hpp"

#include "engine/asset/virtual_file_system.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::scene_internal {
namespace {

gneiss_result order_nodes(const std::vector<object_description>& objects,
                          std::vector<std::size_t>& order, std::size_t maximum_depth,
                          const std::function<bool()>& cancelled,
                          std::size_t* out_depth = nullptr) {
  if (out_depth != nullptr) {
    *out_depth = 0U;
  }
  std::unordered_map<std::string_view, std::size_t> indices;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    if (!indices.emplace(objects[index].uuid, index).second) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
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
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      children[parent->second].push_back(index);
    }
  }
  for (std::size_t cursor = 0; cursor < order.size(); ++cursor) {
    if (cancelled && cancelled()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    const auto index = order[cursor];
    if (depths[index] > maximum_depth) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (out_depth != nullptr) {
      *out_depth = std::max(*out_depth, depths[index]);
    }
    for (const auto child : children[index]) {
      depths[child] = depths[index] + 1U;
      order.push_back(child);
    }
  }
  return order.size() == objects.size() ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_ARGUMENT;
}

class preparation final {
public:
  const asset_internal::virtual_file_system& files;
  const std::function<bool()>& cancelled;
  scene_prepare_limits limits;
  prepared_scene_description candidate;
  scene_diagnostic& diagnostic;
  std::map<std::string, std::vector<std::byte>> sources;
  std::map<std::string, render_internal::render_asset_type> dependencies;
  std::map<std::string, std::size_t> scene_depths;

  gneiss_result read(std::string_view uri, std::string_view& text) {
    if (cancelled && cancelled()) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    diagnostic.path = uri;
    auto& bytes = sources[std::string(uri)];
    auto result = files.read_bounded(uri, limits.source_bytes - candidate.source_bytes, bytes);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    candidate.source_bytes += bytes.size();
    text = {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    return GNEISS_SUCCESS;
  }

  gneiss_result collect(const std::vector<object_description>& objects) {
    using render_internal::render_asset_type;
    for (const auto& object : objects) {
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      if (!object.mesh_renderer) {
        continue;
      }
      for (const auto& asset : {
               render_internal::render_asset_reload{
                   .uri = object.mesh_renderer->mesh_uri,
                   .type = render_asset_type::mesh,
               },
               render_internal::render_asset_reload{
                   .uri = object.mesh_renderer->material_uri,
                   .type = render_asset_type::material,
               },
           }) {
        const auto [entry, added] = dependencies.emplace(asset.uri, asset.type);
        if ((!added && entry->second != asset.type) || dependencies.size() > limits.dependencies) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
      }
    }
    return GNEISS_SUCCESS;
  }

  gneiss_result add_prefab(const prefab_instance_description& instance) {
    std::string_view text;
    auto result = GNEISS_SUCCESS;
    auto found = candidate.prefabs.find(instance.prefab_uri);
    if (found == candidate.prefabs.end()) {
      if (candidate.prefabs.size() == limits.dependencies) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      result = read(instance.prefab_uri, text);
      prepared_prefab_description prefab;
      if (result == GNEISS_SUCCESS) {
        result = parse_prefab_description(text, prefab.description, diagnostic);
      }
      if (result == GNEISS_SUCCESS && prefab.description.objects.size() > limits.nodes) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      if (result == GNEISS_SUCCESS) {
        result = order_nodes(prefab.description.objects, prefab.parent_first,
                             limits.hierarchy_depth, cancelled, &prefab.maximum_depth);
      }
      if (result == GNEISS_SUCCESS) {
        result = collect(prefab.description.objects);
      }
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      found = candidate.prefabs.emplace(instance.prefab_uri, std::move(prefab)).first;
    }
    const auto parent_depth = instance.parent_uuid ? scene_depths.at(*instance.parent_uuid) : 0U;
    if (parent_depth >= limits.hierarchy_depth ||
        found->second.maximum_depth > limits.hierarchy_depth - parent_depth - 1U) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    // 每个 Prefab 实例还有一个容器节点，共享描述不等于共享实例。
    const auto count = found->second.description.objects.size();
    if (count >= limits.nodes - candidate.instance_nodes) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    candidate.instance_nodes += count + 1U;
    return GNEISS_SUCCESS;
  }

  gneiss_result run(std::string_view uri) {
    std::string_view text;
    auto result = read(uri, text);
    if (result == GNEISS_SUCCESS) {
      result = parse_scene_description(text, candidate.description, diagnostic);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    candidate.instance_nodes = candidate.description.objects.size();
    if (candidate.instance_nodes > limits.nodes) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    result = order_nodes(candidate.description.objects, candidate.parent_first,
                         limits.hierarchy_depth, cancelled);
    if (result == GNEISS_SUCCESS) {
      result = collect(candidate.description.objects);
    }
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    for (const auto index : candidate.parent_first) {
      const auto& object = candidate.description.objects[index];
      scene_depths.emplace(object.uuid,
                           object.parent_uuid ? scene_depths.at(*object.parent_uuid) + 1U : 1U);
    }
    for (const auto& instance : candidate.description.prefab_instances) {
      result = add_prefab(instance);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
    for (const auto& [source_uri, expected] : sources) {
      if (cancelled && cancelled()) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      std::vector<std::byte> current;
      if (files.read_bounded(source_uri, expected.size(), current) != GNEISS_SUCCESS ||
          current != expected) {
        diagnostic.path = source_uri;
        diagnostic.message = "Scene/Prefab 源在准备期间变化";
        return GNEISS_ERROR_INVALID_STATE;
      }
    }
    for (const auto& [asset_uri, type] : dependencies) {
      candidate.assets.push_back({.uri = asset_uri, .type = type});
    }
    return GNEISS_SUCCESS;
  }
};

} // namespace

gneiss_result prepare_scene_description(const asset_internal::virtual_file_system& files,
                                        std::string_view uri, prepared_scene_description& output,
                                        scene_diagnostic& diagnostic,
                                        const std::function<bool()>& cancelled,
                                        scene_prepare_limits limits) noexcept {
  try {
    output = {};
    diagnostic = {};
    preparation work{.files = files,
                     .cancelled = cancelled,
                     .limits = limits,
                     .candidate = {},
                     .diagnostic = diagnostic,
                     .sources = {},
                     .dependencies = {},
                     .scene_depths = {}};
    const auto result = work.run(uri);
    if (result == GNEISS_SUCCESS) {
      output = std::move(work.candidate);
    }
    diagnostic.result = result;
    return result;
  } catch (const std::bad_alloc&) {
    diagnostic.result = GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    diagnostic.result = GNEISS_ERROR_INTERNAL;
  }
  return diagnostic.result;
}

} // namespace gneiss::scene_internal
