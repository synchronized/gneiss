// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "world/render_snapshot.h"
#include "world/world_state.h"

int run_tests() {
  gneiss::world_internal::world_state world{1};
  const auto camera_entity = world.create_entity();
  const auto mesh_entity = world.create_entity();
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id mesh_node = GNEISS_NULL_SCENE_NODE_ID;
  const gneiss_camera camera = GNEISS_CAMERA_INIT;
  const gneiss_mesh_renderer renderer{.mesh = UINT64_C(0x1001), .material = UINT64_C(0x2001)};
  gneiss_transform mesh_transform = GNEISS_TRANSFORM_IDENTITY;
  mesh_transform.translation[0] = 2.0F;

  if (world.scene().create(GNEISS_NULL_SCENE_NODE_ID, camera_entity, &camera_node) !=
          GNEISS_SUCCESS ||
      world.scene().create(GNEISS_NULL_SCENE_NODE_ID, mesh_entity, &mesh_node) != GNEISS_SUCCESS ||
      world.scene().set_local(mesh_node, mesh_transform) != GNEISS_SUCCESS) {
    return 1;
  }
  world.emplace<gneiss::world_internal::camera_component>(camera_entity, camera);
  if (world.set_active_camera(camera_entity) != GNEISS_SUCCESS) {
    return 2;
  }
  world.emplace<gneiss::world_internal::mesh_renderer_component>(mesh_entity, renderer);

  gneiss::world_internal::render_snapshot snapshot;
  if (gneiss::world_internal::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          GNEISS_SUCCESS ||
      !snapshot.has_camera || snapshot.instances.size() != 1U ||
      snapshot.camera.viewport_width != 1280U || snapshot.camera.viewport_height != 720U ||
      snapshot.camera.view.values[15] != 1.0F || snapshot.camera.projection.values[0] == 0.0F ||
      snapshot.instances.front().mesh != renderer.mesh ||
      snapshot.instances.front().material != renderer.material ||
      snapshot.instances.front().transform.translation[0] != 2.0F) {
    return 3;
  }
  if (!world.destroy_entity(camera_entity) ||
      gneiss::world_internal::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          GNEISS_SUCCESS ||
      snapshot.has_camera || world.active_camera() != GNEISS_NULL_ENTITY_ID) {
    return 4;
  }
  return 0;
}

int main() {
  try {
    return run_tests();
  } catch (...) {
    return 99;
  }
}
