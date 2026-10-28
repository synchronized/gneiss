// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/world/render_snapshot.hpp"
#include "engine/function/world/world_state.hpp"
#include "engine/resource/render_resource_factory.hpp"
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
  view.camera.near = -1.0F;
  if (application->set_view_camera(&view) != gneiss::engine::result::invalid_argument) {
    return 22;
  }
  gneiss::engine::result foreign_result = gneiss::engine::result::success;
  std::thread foreign([&] { foreign_result = application->set_view_camera(nullptr); });
  foreign.join();
  if (foreign_result != gneiss::engine::result::invalid_state ||
      application->set_view_camera(nullptr) != gneiss::engine::result::success ||
      application->shutdown() != gneiss::engine::result::success ||
      application->set_view_camera(nullptr) != gneiss::engine::result::invalid_state) {
    return 23;
  }
  return 0;
}

int run_tests() {
  gneiss::engine::render_service resources;
  gneiss::engine::rid mesh_id{}, material_id{};
  if (resources.reserve_mesh(&mesh_id).failed() ||
      resources.reserve_material(&material_id).failed())
    return 30;
  gneiss::engine::world_state world{1};
  const auto camera_entity = world.create_entity();
  const auto mesh_entity = world.create_entity();
  gneiss::scene_node_id camera_node = gneiss::null_scene_node_id;
  gneiss::scene_node_id mesh_node = gneiss::null_scene_node_id;
  const gneiss::engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::engine::mesh_renderer renderer{
      .mesh = gneiss::engine::reference_mesh(resources, mesh_id),
      .material = gneiss::engine::reference_material(resources, material_id)};
  gneiss::engine::math::transform mesh_transform = gneiss::engine::math::transform{};
  mesh_transform.translation[0] = 2.0F;

  if (world.scene().create(gneiss::null_scene_node_id, camera_entity, &camera_node) !=
          gneiss::engine::result::success ||
      world.scene().create(gneiss::null_scene_node_id, mesh_entity, &mesh_node) !=
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
      gneiss::engine::math::matrix_elements(snapshot.camera.view)[15] != 1.0F ||
      gneiss::engine::math::matrix_elements(snapshot.camera.projection)[0] == 0.0F ||
      snapshot.instances.front().mesh != renderer.mesh ||
      snapshot.instances.front().material != renderer.material ||
      snapshot.instances.front().transform.translation[0] != 2.0F) {
    return 3;
  }
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
  invalid_view.camera.near = -1.0F;
  if (gneiss::engine::build_render_snapshot(world, 800U, 600U, snapshot, &invalid_view) !=
          gneiss::engine::result::invalid_argument ||
      snapshot.camera.transform.translation[0] != 17.0F) {
    return 7;
  }
  const auto added_mesh = world.create_entity();
  world.emplace<gneiss::engine::mesh_renderer_component>(added_mesh, renderer);
  gneiss::scene_node_id added_node;
  if (world.scene().create({}, added_mesh, &added_node).failed() ||
      world.scene().get_local(added_node) == nullptr ||
      world.scene().get_local_for_entity(added_mesh) == nullptr ||
      gneiss::engine::build_render_snapshot(world, 1280U, 720U, snapshot).failed() ||
      snapshot.instances.size() != 2U || world.active_camera() != camera_entity ||
      world.scene().get_entity(added_node) != added_mesh)
    return 5;
  if (!world.destroy_entity(camera_entity) ||
      gneiss::engine::build_render_snapshot(world, 1280U, 720U, snapshot) !=
          gneiss::engine::result::success ||
      snapshot.has_camera || world.active_camera() != UINT64_C(0)) {
    return 4;
  }
  if (gneiss::engine::build_render_snapshot(world, 640U, 480U, snapshot, &editor_view) !=
          gneiss::engine::result::success ||
      !snapshot.has_camera || snapshot.camera.transform.translation[0] != 17.0F ||
      snapshot.camera.viewport_width != 640U || world.active_camera() != UINT64_C(0)) {
    return 8;
  }
  // ECS 释放后提交快照独立保活；工作线程最后释放只入队，不复活旧 RID。
  renderer = {};
  if (!world.destroy_entity(mesh_entity) || !world.destroy_entity(added_mesh))
    return 31;
  if (resources.drain_retired_resources() != 0U || snapshot.instances.empty())
    return 32;
  std::thread release([&] { snapshot = {}; });
  release.join();
  if (gneiss::engine::reference_mesh(resources, mesh_id) ||
      gneiss::engine::reference_material(resources, material_id))
    return 33;
  if (resources.drain_retired_resources() != 2U || resources.live_resource_count() != 0U)
    return 34;
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
