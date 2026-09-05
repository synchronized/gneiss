// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene/structural_diff.h"

#include <algorithm>
#include <functional>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace {

using gneiss::scene_internal::camera_description;
using gneiss::scene_internal::mesh_renderer_description;
using gneiss::scene_internal::object_description;
using object_index = std::unordered_map<std::string_view, std::size_t>;

[[nodiscard]] bool equal_camera(const std::optional<camera_description>& left,
                                const std::optional<camera_description>& right) noexcept {
  if (left.has_value() != right.has_value()) {
    return false;
  }
  return !left || (left->vertical_field_of_view_radians == right->vertical_field_of_view_radians &&
                   left->near_plane == right->near_plane && left->far_plane == right->far_plane &&
                   left->is_primary == right->is_primary);
}

[[nodiscard]] bool
equal_mesh_renderer(const std::optional<mesh_renderer_description>& left,
                    const std::optional<mesh_renderer_description>& right) noexcept {
  if (left.has_value() != right.has_value()) {
    return false;
  }
  return !left || (left->mesh_uri == right->mesh_uri && left->material_uri == right->material_uri);
}

[[nodiscard]] gneiss_result build_index(const std::vector<object_description>& objects,
                                        object_index& output, std::vector<std::size_t>& depths) {
  output.reserve(objects.size());
  depths.assign(objects.size(), 0U);
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    if (objects[index].uuid.empty() || !output.emplace(objects[index].uuid, index).second) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }

  enum class visit_state : std::uint8_t { unseen, visiting, complete };
  std::vector<visit_state> states(objects.size(), visit_state::unseen);
  const std::function<gneiss_result(std::size_t)> visit = [&](std::size_t index) -> gneiss_result {
    if (states[index] == visit_state::complete) {
      return GNEISS_SUCCESS;
    }
    if (states[index] == visit_state::visiting) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    states[index] = visit_state::visiting;
    if (objects[index].parent_uuid) {
      const auto parent = output.find(*objects[index].parent_uuid);
      if (parent == output.end() || parent->second == index) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      const auto result = visit(parent->second);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
      depths[index] = depths[parent->second] + 1U;
    }
    states[index] = visit_state::complete;
    return GNEISS_SUCCESS;
  };
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    const auto result = visit(index);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
  }
  return GNEISS_SUCCESS;
}

[[nodiscard]] gneiss::scene_internal::structural_change
compare_objects(const object_description& old_object, const object_description& new_object) {
  using gneiss::scene_internal::structural_change;
  auto changes = structural_change::none;
  if (old_object.name != new_object.name) {
    changes = changes | structural_change::name;
  }
  if (old_object.parent_uuid != new_object.parent_uuid) {
    changes = changes | structural_change::parent;
  }
  if (old_object.translation != new_object.translation ||
      old_object.rotation != new_object.rotation || old_object.scale != new_object.scale) {
    changes = changes | structural_change::transform;
  }
  if (!equal_camera(old_object.camera, new_object.camera)) {
    changes = changes | structural_change::camera;
  }
  if (!equal_mesh_renderer(old_object.mesh_renderer, new_object.mesh_renderer)) {
    changes = changes | structural_change::mesh_renderer;
  }
  return changes;
}

} // namespace

namespace gneiss::scene_internal {

gneiss_result build_structural_diff(const std::vector<object_description>& old_objects,
                                    const std::vector<object_description>& new_objects,
                                    structural_diff& output) noexcept {
  output = {};
  try {
    object_index old_index;
    object_index new_index;
    std::vector<std::size_t> old_depths;
    std::vector<std::size_t> new_depths;
    auto result = build_index(old_objects, old_index, old_depths);
    if (result == GNEISS_SUCCESS) {
      result = build_index(new_objects, new_index, new_depths);
    }
    if (result != GNEISS_SUCCESS) {
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
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    output = {};
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    output = {};
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::scene_internal
