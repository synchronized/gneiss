// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "documents/scene_projection.hpp"
#include "components/component_registration.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/instantiation.hpp"
#include <algorithm>
#include <set>
namespace gneiss::editor {
gneiss::error scene_projection::reset(std::span<const gneiss::scene_object_binding> objects) {
  std::map<gneiss::object_address, object_binding> next;
  std::set<std::uint64_t> entities;
  for (const auto& object : objects) {
    if (!object.object || !object.entity || !entities.insert(object.entity.get()).second ||
        !next.emplace(gneiss::object_address{object.object, object.source},
                      object_binding{object.entity})
             .second)
      return gneiss::error::invalid_argument;
  }
  objects_.swap(next);
  return gneiss::error::success;
}
gneiss::error project_scene_document(gneiss::world& world, scene_instance_id& scene,
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
            ? gneiss::result<bool>{false}
            : gneiss::apply_scene(
                  world, scene, associations, *previous, *next, target.assets,
                  [&](const gneiss::composed_object& object, entity_id node,
                      const gneiss::component_description& component) {
                    return !prepare_editor && !object.address.source
                               ? gneiss::result<gneiss::component_description>{component}
                               : editor::prepare_component_editor_value(world, node, component);
                  });
    if (!updated)
      return updated.error();
    if (*updated) {
      projection.replace(std::move(associations));
      return gneiss::error::success;
    }
  }
  auto composed = compose_scene_document(target);
  if (!composed)
    return composed.error();
  auto current = gneiss::scene_instance::borrow(world, scene);
  if (!current)
    return current.error();
  entity_id previous_camera;
  auto status = gneiss::get_active_camera(world, previous_camera);
  if (status.failed() && status != gneiss::error::not_ready)
    return status;
  auto candidate = world.create_scene();
  if (!candidate)
    return candidate.error();
  struct candidate_guard {
    gneiss::world& world;
    gneiss::scene_instance candidate;
    entity_id camera;
    ~candidate_guard() {
      if (candidate.id()) {
        (void)candidate.unload();
        if (camera.get())
          (void)gneiss::set_active_camera(world, camera);
      }
    }
  } guard{world, *candidate, previous_camera};
  auto rebuilt = gneiss::instantiate(*candidate, *composed, target.assets);
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
  return gneiss::error::success;
}
}
