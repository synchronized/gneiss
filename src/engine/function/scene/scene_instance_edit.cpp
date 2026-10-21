// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/scene_instance.hpp"
#include <algorithm>
#include <array>
#include <new>

namespace gneiss::scene_internal {
gneiss_result scene_instance::capture_mesh_binding(scene_node_id node,
                                                   scene_mesh_binding& output) const noexcept {
  output = {};
  const auto found = std::ranges::find(objects_, node.get(), &object::node);
  if (found == objects_.end()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  output = {
      .mesh = render_internal::render_asset_loader::asset_lease(found->mesh),
      .material = render_internal::render_asset_loader::asset_lease(found->material),
  };
  return GNEISS_SUCCESS;
}

gneiss_result scene_instance::replace_mesh_binding(scene_node_id node_value,
                                                   const scene_mesh_binding& expected,
                                                   const scene_mesh_binding& replacement) noexcept
    try {
  const auto node = node_value.get();
  using loader = render_internal::render_asset_loader;
  scene_mesh_binding current;
  const auto captured = capture_mesh_binding(node_value, current);
  if (captured != GNEISS_SUCCESS) {
    return captured;
  }
  if (current.mesh.get() != expected.mesh.get() ||
      current.material.get() != expected.material.get()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  const auto found = std::ranges::find(objects_, node, &scene_instance::object::node);
  gneiss::engine::resource::mesh_renderer_description author{
      .mesh_uri = std::string(replacement.mesh.uri()),
      .material_uri = std::string(replacement.material.uri()),
  };
  const std::array changes{
      world_internal::mesh_renderer_replacement{
          .entity = entity_id{found->entity},
          .expected = {.mesh = expected.mesh.get(), .material = expected.material.get()},
          .replacement = {.mesh = replacement.mesh.get(), .material = replacement.material.get()},
      },
  };
  const auto changed = native_world_.replace_mesh_renderers(changes);
  if (changed != GNEISS_SUCCESS) {
    return changed;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  description_.objects[index].mesh_renderer = std::move(author);
  found->mesh = loader::mesh_lease(replacement.mesh);
  found->material = loader::material_lease(replacement.material);
  return GNEISS_SUCCESS;
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}

bool scene_instance::contains_editable_node(scene_node_id node) const noexcept {
  return std::ranges::find(objects_, node.get(), &object::node) != objects_.end();
}
} // namespace gneiss::scene_internal
