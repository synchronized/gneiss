// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/core/result.hpp"

#include "engine/resource/vfs/virtual_file_system.hpp"

#include <algorithm>
#include <unordered_map>

namespace gneiss::engine::scene {
namespace {

gneiss::engine::core::result
order_nodes(const std::vector<gneiss::engine::resource::object_description>& objects,
            std::vector<std::size_t>& order, std::size_t maximum_depth,
            const std::function<bool()>& cancelled, std::size_t* out_depth = nullptr) {
  if (out_depth != nullptr) {
    *out_depth = 0U;
  }
  std::unordered_map<std::string_view, std::size_t> indices;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    if (!indices.emplace(objects[index].uuid, index).second) {
      return gneiss::engine::core::result::invalid_argument;
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
        return gneiss::engine::core::result::invalid_argument;
      }
      children[parent->second].push_back(index);
    }
  }
  for (std::size_t cursor = 0; cursor < order.size(); ++cursor) {
    if (cancelled && cancelled()) {
      return gneiss::engine::core::result::invalid_state;
    }
    const auto index = order[cursor];
    if (depths[index] > maximum_depth) {
      return gneiss::engine::core::result::invalid_argument;
    }
    if (out_depth != nullptr) {
      *out_depth = std::max(*out_depth, depths[index]);
    }
    for (const auto child : children[index]) {
      depths[child] = depths[index] + 1U;
      order.push_back(child);
    }
  }
  return order.size() == objects.size() ? gneiss::engine::core::result::success
                                        : gneiss::engine::core::result::invalid_argument;
}

class preparation final {
public:
  const engine::resource::virtual_file_system& files;
  const std::function<bool()>& cancelled;
  scene_prepare_limits limits;
  prepared_scene_description candidate;
  gneiss::engine::resource::scene_diagnostic& diagnostic;
  std::vector<std::byte> read_buffer;
  std::map<std::string, engine::render::render_asset_type> dependencies;
  std::map<std::string, std::size_t> scene_depths;

  gneiss::engine::core::result read(std::string_view uri, std::string_view& text) {
    if (cancelled && cancelled()) {
      return gneiss::engine::core::result::invalid_state;
    }
    diagnostic.path = uri;
    auto& bytes = read_buffer;
    auto result = files.read_bounded(uri, limits.source_bytes - candidate.source_bytes, bytes);
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
    candidate.source_bytes += bytes.size();
    text = {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    return gneiss::engine::core::result::success;
  }

  gneiss::engine::core::result
  collect(const std::vector<gneiss::engine::resource::object_description>& objects) {
    using engine::render::render_asset_type;
    for (const auto& object : objects) {
      if (cancelled && cancelled()) {
        return gneiss::engine::core::result::invalid_state;
      }
      if (!object.mesh_renderer) {
        continue;
      }
      for (const auto& asset : {
               engine::render::render_asset_reload{
                   .uri = object.mesh_renderer->mesh_uri,
                   .type = render_asset_type::mesh,
               },
               engine::render::render_asset_reload{
                   .uri = object.mesh_renderer->material_uri,
                   .type = render_asset_type::material,
               },
           }) {
        const auto [entry, added] = dependencies.emplace(asset.uri, asset.type);
        if ((!added && entry->second != asset.type) || dependencies.size() > limits.dependencies) {
          return gneiss::engine::core::result::invalid_argument;
        }
      }
    }
    return gneiss::engine::core::result::success;
  }

  gneiss::engine::core::result
  add_prefab(const gneiss::engine::resource::prefab_instance_description& instance) {
    std::string_view text;
    auto result = gneiss::engine::core::result::success;
    auto found = candidate.prefabs.find(instance.prefab_uri);
    if (found == candidate.prefabs.end()) {
      if (candidate.prefabs.size() == limits.dependencies) {
        return gneiss::engine::core::result::invalid_argument;
      }
      result = read(instance.prefab_uri, text);
      prepared_prefab_description prefab;
      if (result == gneiss::engine::core::result::success) {
        result = gneiss::engine::resource::parse_prefab_description(text, prefab.description,
                                                                    diagnostic);
      }
      if (result == gneiss::engine::core::result::success &&
          prefab.description.objects.size() > limits.nodes) {
        return gneiss::engine::core::result::invalid_argument;
      }
      if (result == gneiss::engine::core::result::success) {
        result = order_nodes(prefab.description.objects, prefab.parent_first,
                             limits.hierarchy_depth, cancelled, &prefab.maximum_depth);
      }
      if (result == gneiss::engine::core::result::success) {
        result = collect(prefab.description.objects);
      }
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
      found = candidate.prefabs.emplace(instance.prefab_uri, std::move(prefab)).first;
    }
    const auto parent_depth = instance.parent_uuid ? scene_depths.at(*instance.parent_uuid) : 0U;
    if (parent_depth >= limits.hierarchy_depth ||
        found->second.maximum_depth > limits.hierarchy_depth - parent_depth - 1U) {
      return gneiss::engine::core::result::invalid_argument;
    }
    // 每个 Prefab 实例还有一个容器节点，共享描述不等于共享实例。
    const auto count = found->second.description.objects.size();
    if (count >= limits.nodes - candidate.instance_nodes) {
      return gneiss::engine::core::result::invalid_argument;
    }
    candidate.instance_nodes += count + 1U;
    return gneiss::engine::core::result::success;
  }

  gneiss::engine::core::result run(std::string_view uri) {
    std::string_view text;
    auto result = read(uri, text);
    if (result == gneiss::engine::core::result::success) {
      result = gneiss::engine::resource::parse_scene_description(text, candidate.description,
                                                                 diagnostic);
    }
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
    candidate.instance_nodes = candidate.description.objects.size();
    if (candidate.instance_nodes > limits.nodes) {
      return gneiss::engine::core::result::invalid_argument;
    }
    result = order_nodes(candidate.description.objects, candidate.parent_first,
                         limits.hierarchy_depth, cancelled);
    if (result == gneiss::engine::core::result::success) {
      result = collect(candidate.description.objects);
    }
    if (result != gneiss::engine::core::result::success) {
      return result;
    }
    for (const auto index : candidate.parent_first) {
      const auto& object = candidate.description.objects[index];
      scene_depths.emplace(object.uuid,
                           object.parent_uuid ? scene_depths.at(*object.parent_uuid) + 1U : 1U);
    }
    for (const auto& instance : candidate.description.prefab_instances) {
      result = add_prefab(instance);
      if (result != gneiss::engine::core::result::success) {
        return result;
      }
    }
    return finish();
  }

  gneiss::engine::core::result finish() {
    if (cancelled && cancelled()) {
      return gneiss::engine::core::result::invalid_state;
    }
    for (const auto& [asset_uri, type] : dependencies) {
      candidate.assets.push_back({.uri = asset_uri, .type = type});
    }
    return gneiss::engine::core::result::success;
  }
};

} // namespace

gneiss::engine::core::result prepare_prefab_description(
    const engine::resource::virtual_file_system& files, std::string_view uri,
    prepared_scene_description& output, gneiss::engine::resource::scene_diagnostic& diagnostic,
    const std::function<bool()>& cancelled, scene_prepare_limits limits) noexcept {
  output = {};
  diagnostic = {};
  try {
    preparation work{.files = files,
                     .cancelled = cancelled,
                     .limits = limits,
                     .candidate = {},
                     .diagnostic = diagnostic,
                     .read_buffer = {},
                     .dependencies = {},
                     .scene_depths = {}};
    gneiss::engine::resource::prefab_instance_description instance;
    instance.prefab_uri = uri;
    auto result = work.add_prefab(instance);
    if (result == gneiss::engine::core::result::success) {
      result = work.finish();
    }
    if (result == gneiss::engine::core::result::success) {
      output = std::move(work.candidate);
    }
    diagnostic.result = result;
  } catch (const std::bad_alloc&) {
    diagnostic.result = gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    diagnostic.result = gneiss::engine::core::result::internal;
  }
  return diagnostic.result;
}

gneiss::engine::core::result prepare_scene_description(
    const engine::resource::virtual_file_system& files, std::string_view uri,
    prepared_scene_description& output, gneiss::engine::resource::scene_diagnostic& diagnostic,
    const std::function<bool()>& cancelled, scene_prepare_limits limits) noexcept {
  try {
    output = {};
    diagnostic = {};
    preparation work{.files = files,
                     .cancelled = cancelled,
                     .limits = limits,
                     .candidate = {},
                     .diagnostic = diagnostic,
                     .read_buffer = {},
                     .dependencies = {},
                     .scene_depths = {}};
    const auto result = work.run(uri);
    if (result == gneiss::engine::core::result::success) {
      output = std::move(work.candidate);
    }
    diagnostic.result = result;
    return result;
  } catch (const std::bad_alloc&) {
    diagnostic.result = gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    diagnostic.result = gneiss::engine::core::result::internal;
  }
  return diagnostic.result;
}

} // namespace gneiss::engine::scene
