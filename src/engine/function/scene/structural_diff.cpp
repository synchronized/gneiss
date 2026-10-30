// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/structural_diff.hpp"
#include "engine/core/error.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/resource/scene/object_hierarchy.hpp"

#include <algorithm>
#include <functional>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace {

using object_index =
    std::unordered_map<gneiss::engine::guid, std::size_t, gneiss::engine::guid_hash>;

[[nodiscard]] gneiss::engine::error
build_index(const std::vector<gneiss::engine::object_description>& objects, object_index& output,
            std::vector<std::size_t>& depths) {
  for (const auto& object : objects) {
    const auto status = gneiss::engine::validate_scene_components(object);
    if (status.failed())
      return status;
  }
  auto hierarchy = gneiss::engine::inspect_object_hierarchy(objects);
  if (!hierarchy)
    return hierarchy.error().error;
  output = std::move(hierarchy->indices);
  depths = std::move(hierarchy->depths);
  return gneiss::engine::error::success;
}

[[nodiscard]] gneiss::engine::structural_change
compare_objects(const gneiss::engine::object_description& old_object,
                const gneiss::engine::object_description& new_object) {
  using gneiss::engine::structural_change;
  auto changes = structural_change::none;
  if (old_object.name != new_object.name) {
    changes = changes | structural_change::name;
  }
  if (old_object.parent_uuid != new_object.parent_uuid) {
    changes = changes | structural_change::parent;
  }
  if (old_object.transform.translation != new_object.transform.translation ||
      old_object.transform.rotation != new_object.transform.rotation ||
      old_object.transform.scale != new_object.transform.scale) {
    changes = changes | structural_change::transform;
  }
  if (!gneiss::engine::equal_components(old_object.components, new_object.components))
    changes = changes | structural_change::components;
  return changes;
}

} // namespace

namespace gneiss::engine {

engine::error build_structural_diff(const std::vector<engine::object_description>& old_objects,
                                    const std::vector<engine::object_description>& new_objects,
                                    structural_diff& output) noexcept {
  output = {};
  try {
    object_index old_index;
    object_index new_index;
    std::vector<std::size_t> old_depths;
    std::vector<std::size_t> new_depths;
    auto result = build_index(old_objects, old_index, old_depths);
    if (result == engine::error::success) {
      result = build_index(new_objects, new_index, new_depths);
    }
    if (result != engine::error::success) {
      return result;
    }

    output.added.reserve(new_objects.size());
    output.updated.reserve(std::min(old_objects.size(), new_objects.size()));
    output.removed.reserve(old_objects.size());
    for (std::size_t index = 0U; index < new_objects.size(); ++index) {
      const auto previous = old_index.find(new_objects[index].uuid);
      if (previous == old_index.end()) {
        output.added.push_back(
            {.uuid = new_objects[index].uuid, .new_index = index, .depth = new_depths[index]});
        continue;
      }
      const auto changes = compare_objects(old_objects[previous->second], new_objects[index]);
      if (changes != structural_change::none) {
        output.updated.push_back({.uuid = new_objects[index].uuid,
                                  .old_index = previous->second,
                                  .new_index = index,
                                  .changes = changes});
      }
    }
    for (std::size_t index = 0U; index < old_objects.size(); ++index) {
      if (!new_index.contains(old_objects[index].uuid)) {
        output.removed.push_back(
            {.uuid = old_objects[index].uuid, .old_index = index, .depth = old_depths[index]});
      }
    }

    std::ranges::sort(output.added, [](const auto& left, const auto& right) {
      return left.depth != right.depth ? left.depth < right.depth : left.uuid < right.uuid;
    });
    std::ranges::sort(output.updated, {}, &structural_node_update::uuid);
    std::ranges::sort(output.removed, [](const auto& left, const auto& right) {
      return left.depth != right.depth ? left.depth > right.depth : left.uuid < right.uuid;
    });
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    output = {};
    return engine::error::out_of_memory;
  } catch (...) {
    output = {};
    return engine::error::internal;
  }
}

} // namespace gneiss::engine
