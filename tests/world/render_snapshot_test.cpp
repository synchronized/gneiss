// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/world/render_snapshot.hpp"
#include "engine/function/world/world_state.hpp"
#include "runtime/application.hpp"
#include <thread>

int run_host_tests() {
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create({}, application).failed()) {
    return 20;
  }
  gneiss::engine::view_camera view;
  if (application->set_view_camera(&view) != gneiss::engine::result::success) {
    return 21;
  }
  view.camera.near_plane = -1.0F;
  if (application->set_view_camera(&view) != gneiss::engine::result::invalid_argument) {
    return 22;
  }
  gneiss::engine::result foreign_result = gneiss::engine::result::success;
  std::thread foreign([&] { foreign_result = application->set_view_camera(nullptr); });
  foreign.join();
  if (foreign_result != gneiss::engine::result::invalid_state ||
      application->set_view_camera(nullptr) != gneiss::engine::result::success ||
      application->shutdown(0) != gneiss::engine::result::success ||
      application->set_view_camera(nullptr) != gneiss::engine::result::invalid_state) {
    return 23;
  }
  return 0;
}

int run_tests() {
  gneiss::engine::world_state world{1};
  const auto camera_entity = world.create_entity();
  const auto mesh_entity = world.create_entity();
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id mesh_node = GNEISS_NULL_SCENE_NODE_ID;
  const gneiss::engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  const gneiss::engine::mesh_renderer renderer{.mesh = UINT64_C(0x1001),
                                               .material = UINT64_C(0x2001)};
  gneiss::engine::transform mesh_transform = gneiss::engine::transform{};
  mesh_transform.translation[0] = 2.0F;

  if (world.scene().create(GNEISS_NULL_SCENE_NODE_ID, camera_entity, &camera_node) !=
          gneiss::engine::result::success ||
      world.scene().create(GNEISS_NULL_SCENE_NODE_ID, mesh_entity, &mesh_node) !=
          gneiss::engine::result::success ||
      world.scene().set_local(mesh_node, mesh_transform) != gneiss::engine::result::success) {
    return 1;
  }
  world.emplace<gneiss::engine::camera_component>(camera_entity, camera);
  if (world.set_active_camera(camera_entity) != gneiss::engine::result::success) {
    return 2;
  }
  world.emplace<gneiss::engine::mesh_renderer_component>(mesh_entity, renderer);

  gneiss::engine::render_snapshot snapshot;
  if (gneiss::engine::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          gneiss::engine::result::success ||
      !snapshot.has_camera || snapshot.instances.size() != 1U ||
      snapshot.camera.viewport_width != 1280U || snapshot.camera.viewport_height != 720U ||
      snapshot.camera.view.values[15] != 1.0F || snapshot.camera.projection.values[0] == 0.0F ||
      snapshot.instances.front().mesh != renderer.mesh ||
      snapshot.instances.front().material != renderer.material ||
      snapshot.instances.front().transform.translation[0] != 2.0F) {
    return 3;
  }
  const auto group = world.begin_publication();
  gneiss::engine::view_camera editor_view;
  editor_view.transform.translation[0] = 17.0F;
  if (gneiss::engine::build_render_snapshot(world, 800U, 600U, snapshot, &editor_view) !=
          gneiss::engine::result::success ||
      !snapshot.has_camera || snapshot.camera.transform.translation[0] != 17.0F ||
      world.active_camera() != camera_entity || snapshot.instances.size() != 1U) {
    return 6;
  }
  // 无效视口输入不能回退到运行时相机，也不能覆盖上一份有效快照。
  auto invalid_view = editor_view;
  invalid_view.camera.near_plane = -1.0F;
  if (gneiss::engine::build_render_snapshot(world, 800U, 600U, snapshot, &invalid_view) !=
          gneiss::engine::result::invalid_argument ||
      snapshot.camera.transform.translation[0] != 17.0F) {
    return 7;
  }
  const auto pending_mesh = world.create_staged_entity(group);
  const auto pending_camera = world.create_staged_entity(group);
  gneiss_scene_node_id pending_node = GNEISS_NULL_SCENE_NODE_ID;
  if (!world.emplace_staged<gneiss::engine::mesh_renderer_component>(group, pending_mesh,
                                                                     renderer) ||
      !world.emplace_staged<gneiss::engine::camera_component>(group, pending_camera, camera) ||
      world.scene().create_staged(group, GNEISS_NULL_SCENE_NODE_ID, pending_mesh, &pending_node) !=
          gneiss::engine::result::success ||
      world.scene().get_local(pending_node) != nullptr ||
      world.scene().get_local_for_entity(pending_mesh) != nullptr ||
      world.scene().get_entity(pending_node) != GNEISS_NULL_ENTITY_ID ||
      world.set_active_camera(pending_camera) != gneiss::engine::result::invalid_handle ||
      world.active_camera() != camera_entity ||
      gneiss::engine::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          gneiss::engine::result::success ||
      snapshot.instances.size() != 1U || world.publish(group) != gneiss::engine::result::success ||
      gneiss::engine::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          gneiss::engine::result::success ||
      snapshot.instances.size() != 2U || world.active_camera() != camera_entity ||
      world.scene().get_entity(pending_node) != pending_mesh) {
    return 5;
  }
  if (!world.destroy_entity(camera_entity) ||
      gneiss::engine::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          gneiss::engine::result::success ||
      snapshot.has_camera || world.active_camera() != GNEISS_NULL_ENTITY_ID) {
    return 4;
  }
  if (gneiss::engine::build_render_snapshot(world, 640U, 480U, snapshot, &editor_view) !=
          gneiss::engine::result::success ||
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
