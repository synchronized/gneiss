// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/structural_diff.hpp"
#include "engine/core/error.hpp"
#include "engine/function/scene/scene_object.hpp"

#include <algorithm>
#include <functional>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace {

using gneiss::engine::camera_description;
using gneiss::engine::mesh_renderer_description;
using gneiss::engine::object_description;
using object_index =
    std::unordered_map<gneiss::engine::guid, std::size_t, gneiss::engine::guid_hash>;

[[nodiscard]] bool equal_camera(const gneiss::engine::camera_description* left,
                                const gneiss::engine::camera_description* right) noexcept {
  if ((left != nullptr) != (right != nullptr)) {
    return false;
  }
  return !left || (left->vertical_field_of_view_radians == right->vertical_field_of_view_radians &&
                   left->near_plane == right->near_plane && left->far_plane == right->far_plane &&
                   left->is_primary == right->is_primary);
}

[[nodiscard]] bool
equal_mesh_renderer(const gneiss::engine::mesh_renderer_description* left,
                    const gneiss::engine::mesh_renderer_description* right) noexcept {
  if ((left != nullptr) != (right != nullptr)) {
    return false;
  }
  return !left || (left->mesh_uri == right->mesh_uri && left->material_uri == right->material_uri);
}

[[nodiscard]] gneiss::engine::error
build_index(const std::vector<gneiss::engine::object_description>& objects, object_index& output,
            std::vector<std::size_t>& depths) {
  for (const auto& object : objects) {
    const auto status = gneiss::engine::validate_scene_components(object);
    if (status.failed())
      return status;
  }
  output.reserve(objects.size());
  depths.assign(objects.size(), 0U);
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    if (objects[index].uuid.is_nil() || !output.emplace(objects[index].uuid, index).second) {
      return gneiss::engine::error::invalid_argument;
    }
  }

  enum class visit_state : std::uint8_t { unseen, visiting, complete };
  std::vector<visit_state> states(objects.size(), visit_state::unseen);
  const std::function<gneiss::engine::error(std::size_t)> visit =
      [&](std::size_t index) -> gneiss::engine::error {
    if (states[index] == visit_state::complete) {
      return gneiss::engine::error::success;
    }
    if (states[index] == visit_state::visiting) {
      return gneiss::engine::error::invalid_argument;
    }
    states[index] = visit_state::visiting;
    if (objects[index].parent_uuid) {
      const auto parent = output.find(*objects[index].parent_uuid);
      if (parent == output.end() || parent->second == index) {
        return gneiss::engine::error::invalid_argument;
      }
      const auto result = visit(parent->second);
      if (result != gneiss::engine::error::success) {
        return result;
      }
      depths[index] = depths[parent->second] + 1U;
    }
    states[index] = visit_state::complete;
    return gneiss::engine::error::success;
  };
  for (std::size_t index = 0U; index < objects.size(); ++index) {
    const auto result = visit(index);
    if (result != gneiss::engine::error::success) {
      return result;
    }
  }
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
  if (!equal_camera(old_object.components.template get<gneiss::engine::camera_description>(),
                    new_object.components.template get<gneiss::engine::camera_description>())) {
    changes = changes | structural_change::camera;
  }
  if (!equal_mesh_renderer(
          old_object.components.template get<gneiss::engine::mesh_renderer_description>(),
          new_object.components.template get<gneiss::engine::mesh_renderer_description>())) {
    changes = changes | structural_change::mesh_renderer;
  }
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
