// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/core/math/camera.hpp>
#include "support/value_result.hpp"
#include <thread>

int main(void) {
  if (gneiss::initialize_meta().failed())
    return 1;
  gneiss::world_owner first;

  gneiss::world_owner second;

  if (gneiss::test_support::assign_value(gneiss::create_native(), first) !=
          gneiss::error::success ||
      gneiss::test_support::assign_value(gneiss::create_native(), second) !=
          gneiss::error::success) {
    return 1;
  }

  auto empty_count = first->entity_count();
  if (empty_count != 0U || first->find({}).is_valid())
    return 30;
  auto scene = first->create_scene();
  if (!scene)
    return 2;
  auto created = scene->create_object();
  if (!created)
    return 2;
  auto created_object = *created;
  auto entity = created_object.entity();
  if (first->entity_count() != 1U) {
    return 3;
  }
  gneiss::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::math::camera_settings queried_camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::entity_id active_camera = gneiss::entity_id{};
  if (gneiss::get_active_camera(*first, active_camera) !=
          gneiss::error::not_ready ||
      active_camera != gneiss::entity_id{} ||
      created_object.add_component<gneiss::camera_component>(camera).failed() ||
      gneiss::test_support::read_component_value(
          created_object.try_component<gneiss::camera_component>(), queried_camera) !=
          gneiss::error::success ||
      queried_camera.near_plane != camera.near_plane ||
      gneiss::set_active_camera(*second, entity) != gneiss::error::invalid_handle ||
      gneiss::set_active_camera(*first, entity) != gneiss::error::success ||
      gneiss::get_active_camera(*first, active_camera) != gneiss::error::success ||
      active_camera != entity) {
    return 4;
  }
  if (created_object.remove_component<gneiss::camera_component>().failed() ||
      created_object.remove_component<gneiss::camera_component>() !=
          gneiss::error::not_found ||
      gneiss::get_active_camera(*first, active_camera) !=
          gneiss::error::not_ready ||
      gneiss::set_active_camera(*first, entity) != gneiss::error::not_ready ||
      created_object.add_component<gneiss::camera_component>(camera).failed() ||
      gneiss::set_active_camera(*first, entity) != gneiss::error::success) {
    return 5;
  }
  const auto object = first->find(entity);
  queried_camera.near_plane = 0.25F;
  if (object.add_component<gneiss::camera_component>(queried_camera) !=
          gneiss::error::invalid_state ||
      !object.try_component<gneiss::camera_component>() ||
      object.try_component<gneiss::camera_component>()->settings.near_plane != camera.near_plane)
    return 36;
  auto* borrowed = object.try_component<gneiss::camera_component>();
  if (!borrowed)
    return 31;
  auto captured = *borrowed;
  auto settings = captured.settings;
  settings.near_plane = 0.5F;
  if (!(captured.settings = settings).valid() || borrowed->settings.near_plane != camera.near_plane)
    return 32;
  if (second->find(entity).is_valid())
    return 33;
  gneiss::error thread_status;
  std::thread worker(
      [&] { thread_status = object.add_component<gneiss::camera_component>(captured); });
  worker.join();
  if (thread_status != gneiss::error::invalid_state)
    return 34;
  if (second->object(entity).error() != gneiss::error::invalid_handle)
    return 6;
  first->find(entity).destroy();
  if (first->object(entity).error() != gneiss::error::invalid_handle) {
    return 6;
  }
  if (object.is_valid() || object.remove_component<gneiss::camera_component>() !=
                               gneiss::error::invalid_handle)
    return 35;
  if (first->find(entity).is_valid() || gneiss::get_active_camera(*first, active_camera) !=
                                            gneiss::error::not_ready) {
    return 7;
  }
  first.reset();
  second.reset();
  return 0;
}
