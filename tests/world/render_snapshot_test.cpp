// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/application/application_view_internal.hpp"
#include "engine/function/world/render_snapshot.hpp"
#include "engine/function/world/world_state.hpp"
#include <thread>

int run_host_tests() {
  using gneiss::application_internal::set_view_camera;
  gneiss_application application{};
  const gneiss_application_desc description = GNEISS_APPLICATION_DESC_INIT;
  if (gneiss_application_create(&description, &application) != GNEISS_SUCCESS) {
    return 20;
  }
  gneiss::render_internal::view_camera view;
  if (set_view_camera(application, &view) != GNEISS_SUCCESS) {
    return 21;
  }
  view.camera.near_plane = -1.0F;
  if (set_view_camera(application, &view) != GNEISS_ERROR_INVALID_ARGUMENT) {
    return 22;
  }
  gneiss_result foreign_result = GNEISS_SUCCESS;
  std::thread foreign([&] { foreign_result = set_view_camera(application, nullptr); });
  foreign.join();
  if (foreign_result != GNEISS_ERROR_INVALID_STATE ||
      set_view_camera(application, nullptr) != GNEISS_SUCCESS ||
      gneiss_application_destroy(application) != GNEISS_SUCCESS ||
      set_view_camera(application, nullptr) != GNEISS_ERROR_INVALID_HANDLE) {
    return 23;
  }
  return 0;
}

int run_tests() {
  gneiss::world_internal::world_state world{1};
  const auto camera_entity = world.create_entity();
  const auto mesh_entity = world.create_entity();
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id mesh_node = GNEISS_NULL_SCENE_NODE_ID;
  const gneiss::render_internal::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  const gneiss::world_internal::mesh_renderer renderer{.mesh = UINT64_C(0x1001),
                                                       .material = UINT64_C(0x2001)};
  gneiss::engine::transform mesh_transform = gneiss::engine::transform{};
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

  gneiss::render_internal::render_snapshot snapshot;
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
  const auto group = world.begin_publication();
  gneiss::render_internal::view_camera editor_view;
  editor_view.transform.translation[0] = 17.0F;
  if (gneiss::world_internal::build_render_snapshot(world, 800U, 600U, snapshot, &editor_view) !=
          GNEISS_SUCCESS ||
      !snapshot.has_camera || snapshot.camera.transform.translation[0] != 17.0F ||
      world.active_camera() != camera_entity || snapshot.instances.size() != 1U) {
    return 6;
  }
  // 无效视口输入不能回退到运行时相机，也不能覆盖上一份有效快照。
  auto invalid_view = editor_view;
  invalid_view.camera.near_plane = -1.0F;
  if (gneiss::world_internal::build_render_snapshot(world, 800U, 600U, snapshot, &invalid_view) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      snapshot.camera.transform.translation[0] != 17.0F) {
    return 7;
  }
  const auto pending_mesh = world.create_staged_entity(group);
  const auto pending_camera = world.create_staged_entity(group);
  gneiss_scene_node_id pending_node = GNEISS_NULL_SCENE_NODE_ID;
  if (!world.emplace_staged<gneiss::world_internal::mesh_renderer_component>(group, pending_mesh,
                                                                             renderer) ||
      !world.emplace_staged<gneiss::world_internal::camera_component>(group, pending_camera,
                                                                      camera) ||
      world.scene().create_staged(group, GNEISS_NULL_SCENE_NODE_ID, pending_mesh, &pending_node) !=
          GNEISS_SUCCESS ||
      world.scene().get_local(pending_node) != nullptr ||
      world.scene().get_local_for_entity(pending_mesh) != nullptr ||
      world.scene().get_entity(pending_node) != GNEISS_NULL_ENTITY_ID ||
      world.set_active_camera(pending_camera) != GNEISS_ERROR_INVALID_HANDLE ||
      world.active_camera() != camera_entity ||
      gneiss::world_internal::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          GNEISS_SUCCESS ||
      snapshot.instances.size() != 1U || world.publish(group) != GNEISS_SUCCESS ||
      gneiss::world_internal::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          GNEISS_SUCCESS ||
      snapshot.instances.size() != 2U || world.active_camera() != camera_entity ||
      world.scene().get_entity(pending_node) != pending_mesh) {
    return 5;
  }
  if (!world.destroy_entity(camera_entity) ||
      gneiss::world_internal::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          GNEISS_SUCCESS ||
      snapshot.has_camera || world.active_camera() != GNEISS_NULL_ENTITY_ID) {
    return 4;
  }
  if (gneiss::world_internal::build_render_snapshot(world, 640U, 480U, snapshot, &editor_view) !=
          GNEISS_SUCCESS ||
      !snapshot.has_camera || snapshot.camera.transform.translation[0] != 17.0F ||
      snapshot.camera.viewport_width != 640U || world.active_camera() != GNEISS_NULL_ENTITY_ID) {
    return 8;
  }
  return 0;
}

int main() {
  try {
    const auto result = run_tests();
    return result == 0 ? run_host_tests() : result;
  } catch (...) {
    return 99;
  }
}
