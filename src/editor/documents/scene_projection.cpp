// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_projection.hpp"
#include "components/component_registration.hpp"
#include "engine/function/scene/component_adapter.hpp"
#include "engine/function/scene/scene_instantiation.hpp"
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
engine::error project_scene_document(engine::world& world, scene_instance_id& scene,
                                     scene_projection& projection,
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
                  world, scene, associations, *previous, *next, target.assets,
                  [&](const engine::composed_object& object, scene_node_id node,
                      const engine::component_description& component) {
                    return !prepare_editor && !object.address.source
                               ? engine::result<engine::component_description>{component}
                               : editor::prepare_component_editor_value(world, node, component);
                  });
    if (!updated)
      return updated.error();
    if (*updated) {
      projection.replace(std::move(associations));
      return engine::error::success;
    }
  }
  auto composed = compose_scene_document(target);
  if (!composed)
    return composed.error();
  auto current = engine::scene_instance::borrow(world, scene);
  if (!current)
    return current.error();
  entity_id previous_camera;
  auto status = world.get_active_camera(previous_camera);
  if (status.failed() && status != engine::error::not_ready)
    return status;
  auto candidate = world.create_scene();
  if (!candidate)
    return candidate.error();
  struct candidate_guard {
    engine::world& world;
    engine::scene_instance candidate;
    entity_id camera;
    ~candidate_guard() {
      if (candidate.id()) {
        (void)candidate.unload();
        if (camera.get())
          (void)world.set_active_camera(camera);
      }
    }
  } guard{world, *candidate, previous_camera};
  auto rebuilt = engine::instantiate(*candidate, *composed, target.assets);
  if (!rebuilt)
    return rebuilt.error();
  scene_projection replacement;
  status = replacement.reset(rebuilt->objects);
  if (status.failed())
    return status;
  auto associations = replacement.objects();
  status = current->unload();
  if (status.failed())
    return status;
  scene = rebuilt->instance;
  guard.candidate = {};
  projection.replace(std::move(associations));
  return engine::error::success;
}
}
