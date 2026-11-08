// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/scene/scene_instance_state.hpp"
#include "engine/function/scene/scene_object.hpp"
#include "engine/function/world/component_identity.hpp"
#include <algorithm>
#include <array>
#include <new>
#include <unordered_set>

namespace gneiss::engine {
error scene_instance_state::add_component_description(scene_node_id node,
                                                      const component_description& value,
                                                      guid identity,
                                                      const scene_mesh_binding* prepared) noexcept {
  if (!value)
    return error::invalid_argument;
  if (!contains_editable_node(node))
    return error::invalid_handle;
  if (prepared && ((prepared->mesh && !loader_.owns(prepared->mesh)) ||
                   (prepared->material && !loader_.owns(prepared->material))))
    return error::invalid_argument;
  const mesh_renderer renderer =
      prepared ? mesh_renderer{prepared->mesh, prepared->material} : mesh_renderer{};
  return add_component({native_world_, node, renderer, prepared ? nullptr : &loader_}, value,
                       identity);
}
error scene_instance_state::remove_component_description(scene_node_id node, guid type) noexcept {
  if (!contains_editable_node(node))
    return error::invalid_handle;
  return remove_component({native_world_, node, {}, &loader_}, type);
}
error scene_instance_state::set_component_description(scene_node_id node,
                                                      const component_description& value,
                                                      const scene_mesh_binding* prepared) noexcept
    try {
  if (!value)
    return error::invalid_argument;
  if (!contains_editable_node(node))
    return error::invalid_handle;
  auto components = capture_components(native_world_, node);
  if (!components)
    return components.error();
  if (!components->find(value->guid()))
    return error::not_found;
  auto renderer = native_world_.node_mesh_renderer(node);
  if (!renderer && renderer.error() != error::not_found)
    return renderer.error();
  if (prepared && ((prepared->mesh && !loader_.owns(prepared->mesh)) ||
                   (prepared->material && !loader_.owns(prepared->material))))
    return error::invalid_argument;
  component_context context{native_world_, node,
                            prepared   ? mesh_renderer{prepared->mesh, prepared->material}
                            : renderer ? *renderer
                                       : mesh_renderer{},
                            prepared ? nullptr : &loader_};
  return update_component(context, value);
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<scene_mesh_binding>
scene_instance_state::capture_mesh_binding(scene_node_id node) const noexcept {
  const auto found = std::ranges::find(objects_, node, &node_entry::node);
  if (found == objects_.end()) {
    return error::invalid_handle;
  }
  auto renderer = native_world_.node_mesh_renderer(node);
  if (!renderer)
    return renderer.error() == error::not_found ? result<scene_mesh_binding>{scene_mesh_binding{}}
                                                : result<scene_mesh_binding>{renderer.error()};
  return scene_mesh_binding{renderer->mesh, renderer->material};
}

error scene_instance_state::replace_mesh_binding(scene_node_id node_value,
                                                 const scene_mesh_binding& expected,
                                                 const scene_mesh_binding& replacement,
                                                 guid component_uuid) noexcept try {
  if ((replacement.mesh && !loader_.owns(replacement.mesh)) ||
      (replacement.material && !loader_.owns(replacement.material))) {
    return error::invalid_argument;
  }
  const auto node = node_value;
  const auto captured = capture_mesh_binding(node_value);
  if (!captured)
    return captured.error();
  const auto& current = *captured;
  if (current.mesh != expected.mesh || current.material != expected.material) {
    return error::invalid_state;
  }
  if (!contains_editable_node(node))
    return error::invalid_handle;
  const std::array changes{
      engine::mesh_renderer_replacement{
          .entity = entity_id{scene_object_entity(native_world_, node)},
          .expected = {.mesh = expected.mesh, .material = expected.material},
          .replacement = {.mesh = replacement.mesh, .material = replacement.material},
      },
  };
  auto existing = native_world_.node_mesh_renderer(node);
  if (!existing && existing.error() != error::not_found)
    return existing.error();
  auto changed = error::success;
  if (!existing) {
    changed = add_component({native_world_,
                             node,
                             {.mesh = replacement.mesh, .material = replacement.material},
                             &loader_},
                            make_component_description(mesh_renderer_description{
                                std::string(resource_uri(replacement.mesh)),
                                std::string(resource_uri(replacement.material))}),
                            component_uuid);
  } else {
    changed = native_world_.replace_mesh_renderers(changes);
  }
  if (changed != error::success) {
    return changed;
  }
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

bool scene_instance_state::contains_editable_node(scene_node_id node) const noexcept {
  return std::ranges::find(objects_, node, &node_entry::node) != objects_.end();
}
} // namespace gneiss::engine
