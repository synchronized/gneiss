// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include <algorithm>
#include <array>
#include <new>

namespace gneiss::engine {
result scene_instance::capture_mesh_binding(scene_node_id node,
                                            scene_mesh_binding& output) const noexcept {
  output = {};
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return result::invalid_handle;
  }
  output = {
      .mesh = found->mesh,
      .material = found->material,
  };
  return result::success;
}

result scene_instance::replace_mesh_binding(scene_node_id node_value,
                                            const scene_mesh_binding& expected,
                                            const scene_mesh_binding& replacement) noexcept try {
  if (engine::asset_kind(replacement.mesh) != asset_type::mesh ||
      engine::asset_kind(replacement.material) != asset_type::material ||
      !loader_.owns(replacement.mesh) || !loader_.owns(replacement.material)) {
    return result::invalid_argument;
  }
  const auto node = node_value;
  scene_mesh_binding current;
  const auto captured = capture_mesh_binding(node_value, current);
  if (captured != result::success) {
    return captured;
  }
  if (current.mesh != expected.mesh || current.material != expected.material) {
    return result::invalid_state;
  }
  const auto found = std::ranges::find(objects_, node, &scene_instance::object::node);
  mesh_renderer_description author{
      .mesh_uri = std::string(engine::asset_uri(replacement.mesh)),
      .material_uri = std::string(engine::asset_uri(replacement.material)),
  };
  const std::array changes{
      engine::mesh_renderer_replacement{
          .entity = entity_id{found->entity},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material},
      },
  };
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  auto changed = result::success;
  if (!expected.mesh && !expected.material && !description_.objects[index].mesh_renderer) {
    changed = native_world_.entity_set_mesh_renderer(
        entity_id{found->entity}, {.mesh = replacement.mesh, .material = replacement.material});
  } else {
    changed = native_world_.replace_mesh_renderers(changes);
  }
  if (changed != result::success) {
    return changed;
  }
  description_.objects[index].mesh_renderer = std::move(author);
  found->mesh = replacement.mesh;
  found->material = replacement.material;
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

bool scene_instance::contains_editable_node(scene_node_id node) const noexcept {
  return std::ranges::find(objects_, node, &object::node) != objects_.end();
}
} // namespace gneiss::engine
