// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_projection.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include <algorithm>
#include <set>
namespace gneiss::editor {
engine::error scene_projection::reset(std::span<const engine::scene_object_binding> objects) {
  std::map<engine::object_address, object_binding> next;
  std::set<std::uint64_t> entities;
  for (const auto& object : objects) {
    if (!object.object || !object.node || object.entity.get() != object.node.get() ||
        !entities.insert(object.entity.get()).second ||
        !next.emplace(engine::object_address{object.object, object.source},
                      object_binding{object.node, object.entity})
             .second)
      return engine::error::invalid_argument;
  }
  objects_.swap(next);
  return engine::error::success;
}
engine::error project_scene_document(engine::world& world, engine::scene_instances& scenes,
                                     scene_instance_id scene, scene_projection& projection,
                                     const scene_document_snapshot* baseline,
                                     const scene_document_snapshot& target, bool prepare_editor) {
  if (baseline) {
    auto associations = projection.objects();
    auto previous = compose_scene_document(*baseline);
    auto next = compose_scene_document(target);
    if (!previous)
      return previous.error();
    if (!next)
      return next.error();
    auto updated =
        baseline->content.get().uuid != target.content.get().uuid
            ? engine::result<bool>{false}
            : engine::apply_scene(
                  world, scenes, scene, associations, *previous, *next, target.assets,
                  [&](const engine::composed_object& object, scene_node_id node,
                      const engine::component_description& component) {
                    return !prepare_editor && !object.address.source
                               ? engine::result<engine::component_description>{component}
                               : engine::prepare_component_editor_value(world, node, component);
                  });
    if (!updated)
      return updated.error();
    if (*updated) {
      projection.replace(std::move(associations));
      return engine::error::success;
    }
  }
  auto inputs = make_scene_document_inputs(target);
  if (!inputs)
    return inputs.error();
  engine::scene_instantiation_data prepared;
  engine::scene_diagnostic diagnostic;
  auto status = engine::prepare_loaded_scene(*inputs, {}, prepared, diagnostic, {});
  if (status.failed())
    return status;
  auto rebuilt = scenes.replace_prepared(scene, std::move(prepared), target.assets);
  if (!rebuilt)
    return rebuilt.error();
  return projection.reset(rebuilt->objects);
}
}
