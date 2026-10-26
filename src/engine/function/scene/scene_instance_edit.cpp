// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include <algorithm>
#include <array>
#include <new>

namespace gneiss::engine {
gneiss::engine::result
scene_instance::capture_mesh_binding(scene_node_id node,
                                     scene_mesh_binding& output) const noexcept {
  output = {};
  const auto found = std::ranges::find(objects_, node, &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::result::invalid_handle;
  }
  output = {
      .mesh = found->mesh,
      .material = found->material,
  };
  return gneiss::engine::result::success;
}

gneiss::engine::result
scene_instance::replace_mesh_binding(scene_node_id node_value, const scene_mesh_binding& expected,
                                     const scene_mesh_binding& replacement) noexcept try {
  if (gneiss::engine::asset_kind(replacement.mesh) != gneiss::engine::render_asset_type::mesh ||
      gneiss::engine::asset_kind(replacement.material) !=
          gneiss::engine::render_asset_type::material ||
      !loader_.owns(replacement.mesh) || !loader_.owns(replacement.material)) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto node = node_value;
  scene_mesh_binding current;
  const auto captured = capture_mesh_binding(node_value, current);
  if (captured != gneiss::engine::result::success) {
    return captured;
  }
  if (current.mesh != expected.mesh || current.material != expected.material) {
    return gneiss::engine::result::invalid_state;
  }
  const auto found = std::ranges::find(objects_, node, &scene_instance::object::node);
  gneiss::engine::mesh_renderer_description author{
      .mesh_uri = std::string(gneiss::engine::asset_uri(replacement.mesh)),
      .material_uri = std::string(gneiss::engine::asset_uri(replacement.material)),
  };
  const std::array changes{
      gneiss::engine::mesh_renderer_replacement{
          .entity = entity_id{found->entity},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material},
      },
  };
  const auto changed = native_world_.replace_mesh_renderers(changes);
  if (changed != gneiss::engine::result::success) {
    return changed;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  description_.objects[index].mesh_renderer = std::move(author);
  found->mesh = replacement.mesh;
  found->material = replacement.material;
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

bool scene_instance::contains_editable_node(scene_node_id node) const noexcept {
  return std::ranges::find(objects_, node, &object::node) != objects_.end();
}
} // namespace gneiss::engine
