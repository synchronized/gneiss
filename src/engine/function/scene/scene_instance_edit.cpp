// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/scene/scene_instance.hpp"
#include <algorithm>
#include <array>
#include <new>

namespace gneiss::engine::scene {
gneiss::engine::core::result
scene_instance::capture_mesh_binding(scene_node_id node,
                                     scene_mesh_binding& output) const noexcept {
  output = {};
  const auto found = std::ranges::find(objects_, node.get(), &object::node);
  if (found == objects_.end()) {
    return gneiss::engine::core::result::invalid_handle;
  }
  output = {
      .mesh = found->mesh,
      .material = found->material,
  };
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result
scene_instance::replace_mesh_binding(scene_node_id node_value, const scene_mesh_binding& expected,
                                     const scene_mesh_binding& replacement) noexcept try {
  if (engine::render::asset_kind(replacement.mesh) != engine::render::render_asset_type::mesh ||
      engine::render::asset_kind(replacement.material) !=
          engine::render::render_asset_type::material ||
      !loader_.owns(replacement.mesh) || !loader_.owns(replacement.material)) {
    return gneiss::engine::core::result::invalid_argument;
  }
  const auto node = node_value.get();
  using loader = engine::render::render_asset_loader;
  scene_mesh_binding current;
  const auto captured = capture_mesh_binding(node_value, current);
  if (captured != gneiss::engine::core::result::success) {
    return captured;
  }
  if (engine::render::asset_rid(current.mesh) != engine::render::asset_rid(expected.mesh) ||
      engine::render::asset_rid(current.material) != engine::render::asset_rid(expected.material)) {
    return gneiss::engine::core::result::invalid_state;
  }
  const auto found = std::ranges::find(objects_, node, &scene_instance::object::node);
  gneiss::engine::resource::mesh_renderer_description author{
      .mesh_uri = std::string(gneiss::engine::render::asset_uri(replacement.mesh)),
      .material_uri = std::string(gneiss::engine::render::asset_uri(replacement.material)),
  };
  const std::array changes{
      engine::world::mesh_renderer_replacement{
          .entity = entity_id{found->entity},
          .expected = {.mesh = engine::render::asset_rid(expected.mesh),
                       .material = engine::render::asset_rid(expected.material)},
          .replacement = {.mesh = engine::render::asset_rid(replacement.mesh),
                          .material = engine::render::asset_rid(replacement.material)},
      },
  };
  const auto changed = native_world_.replace_mesh_renderers(changes);
  if (changed != gneiss::engine::core::result::success) {
    return changed;
  }
  const auto index = static_cast<std::size_t>(std::distance(objects_.begin(), found));
  description_.objects[index].mesh_renderer = std::move(author);
  found->mesh = loader::mesh_ref(replacement.mesh);
  found->material = loader::material_ref(replacement.material);
  return gneiss::engine::core::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::core::result::out_of_memory;
} catch (...) {
  return gneiss::engine::core::result::internal;
}

bool scene_instance::contains_editable_node(scene_node_id node) const noexcept {
  return std::ranges::find(objects_, node.get(), &object::node) != objects_.end();
}
} // namespace gneiss::engine::scene
