// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/asset/detail/asset_construction.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/render_extraction.hpp"
#include "engine/function/scene/world.hpp"
#include "runtime/application.hpp"
#include <thread>

int run_host_tests() {
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create({}), application)
          .failed()) {
    return 20;
  }
  gneiss::engine::view_camera view;
  if (application->set_view_camera(&view) != gneiss::engine::error::success) {
    return 21;
  }
  auto& owner = *application->native_world();
  auto extracted = gneiss::engine::extract_render_snapshot(owner, 800, 600, &view);
  if (!extracted || !extracted->has_camera || extracted->camera.viewport_width != 800 ||
      gneiss::engine::extract_render_snapshot(owner, 0, 600).error() !=
          gneiss::engine::error::invalid_argument)
    return 24;
  gneiss::engine::error extraction_error;
  std::thread extraction_worker(
      [&] { extraction_error = gneiss::engine::extract_render_snapshot(owner, 800, 600).error(); });
  extraction_worker.join();
  if (extraction_error != gneiss::engine::error::invalid_state)
    return 25;
  view.camera.near = -1.0F;
  if (application->set_view_camera(&view) != gneiss::engine::error::invalid_argument) {
    return 22;
  }
  gneiss::engine::error foreign_result = gneiss::engine::error::success;
  std::thread foreign([&] { foreign_result = application->set_view_camera(nullptr); });
  foreign.join();
  if (foreign_result != gneiss::engine::error::invalid_state ||
      application->set_view_camera(nullptr) != gneiss::engine::error::success ||
      application->shutdown() != gneiss::engine::error::success ||
      application->set_view_camera(nullptr) != gneiss::engine::error::invalid_state) {
    return 23;
  }
  return 0;
}

int run_tests() {
  gneiss::engine::render_service resources;
  gneiss::engine::detail::asset_construction resources_factory{resources};
  gneiss::engine::ref<const gneiss::engine::mesh> mesh_ref;
  gneiss::engine::ref<const gneiss::engine::material> material_ref;
  if (gneiss::test_support::take_value(resources_factory.reserve_mesh(), mesh_ref).failed() ||
      gneiss::test_support::take_value(resources_factory.reserve_material(), material_ref).failed())
    return 30;
  const auto mesh_id = resource_handle(mesh_ref);
  const auto material_id = resource_handle(material_ref);
  using namespace gneiss::engine;
  if (initialize_meta().failed())
    return 1;
  auto owner = create_native();
  if (!owner)
    return 1;
  auto& world = **owner;
  auto scene = world.create_scene();
  if (!scene)
    return 1;
  auto camera_object = scene->create_object({"camera", {}});
  auto mesh_object = scene->create_object({"mesh", {}});
  if (!camera_object || !mesh_object)
    return 1;
  const auto camera_entity = camera_object->entity();
  const math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  mesh_renderer renderer{.mesh = mesh_ref, .material = material_ref};
  math::transform mesh_transform;
  mesh_transform.translation.x = 2;
  if (mesh_object->set_local_transform(mesh_transform).failed() ||
      camera_object->set_component(camera_component{camera}).failed() ||
      gneiss::engine::set_active_camera(world, camera_entity).failed() ||
      mesh_object->set_component(mesh_renderer_component{renderer}).failed())
    return 2;
  auto active_camera = [&] {
    gneiss::entity_id id;
    (void)gneiss::engine::get_active_camera(world, id);
    return id;
  };
  auto snapshot = extract_render_snapshot(world, 1280U, 720U);
  if (!snapshot || !snapshot->has_camera || snapshot->instances.size() != 1U ||
      snapshot->camera.viewport_width != 1280U || snapshot->camera.viewport_height != 720U ||
      gneiss::engine::math::matrix_elements(snapshot->camera.view)[15] != 1.0F ||
      gneiss::engine::math::matrix_elements(snapshot->camera.projection)[0] == 0.0F ||
      snapshot->instances.front().mesh != renderer.mesh ||
      snapshot->instances.front().material != renderer.material ||
      snapshot->instances.front().transform.translation[0] != 2.0F) {
    return 3;
  }
  gneiss::engine::view_camera editor_view;
  editor_view.transform.translation[0] = 17.0F;
  snapshot = extract_render_snapshot(world, 800U, 600U, &editor_view);
  if (!snapshot || !snapshot->has_camera || snapshot->camera.transform.translation[0] != 17.0F ||
      active_camera() != camera_entity || snapshot->instances.size() != 1U) {
    return 6;
  }
  // 无效视口输入不能回退到运行时相机，也不能覆盖上一份有效快照。
  auto invalid_view = editor_view;
  invalid_view.camera.near = -1.0F;
  if (extract_render_snapshot(world, 800U, 600U, &invalid_view).error() !=
          gneiss::engine::error::invalid_argument ||
      snapshot->camera.transform.translation[0] != 17.0F) {
    return 7;
  }
  auto added_object = scene->create_object({"added mesh", {}});
  if (!added_object || added_object->set_component(mesh_renderer_component{renderer}).failed())
    return 5;
  snapshot = extract_render_snapshot(world, 1280U, 720U);
  if (!snapshot || snapshot->instances.size() != 2 || active_camera() != camera_entity)
    return 5;
  if (camera_object->destroy().failed())
    return 4;
  snapshot = extract_render_snapshot(world, 1280U, 720U);
  if (!snapshot || snapshot->has_camera || active_camera())
    return 4;
  snapshot = extract_render_snapshot(world, 640U, 480U, &editor_view);
  if (!snapshot || !snapshot->has_camera || snapshot->camera.transform.translation.x != 17 ||
      snapshot->camera.viewport_width != 640 || active_camera())
    return 8;
  // ECS 释放后提交快照独立保活；工作线程最后释放只入队，不复活旧 RID。
  renderer = {};
  mesh_ref.reset();
  material_ref.reset();
  if (mesh_object->destroy().failed() || added_object->destroy().failed())
    return 31;
  if (resources_factory.drain() != 0U || snapshot->instances.empty())
    return 32;
  std::thread release([&] { snapshot->instances.clear(); });
  release.join();
  if (resources_factory.find_mesh(mesh_id) || resources_factory.find_material(material_id))
    return 33;
  if (resources_factory.drain() != 2U || resources.live_resource_count() != 0U)
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
