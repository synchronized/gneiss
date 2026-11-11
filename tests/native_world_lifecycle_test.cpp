// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/world.hpp"
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
  if (!empty_count || *empty_count != 0U || first->find({}).is_valid())
    return 30;
  auto scene = first->create_scene();
  if (!scene)
    return 2;
  auto created = scene->create_object();
  if (!created)
    return 2;
  auto entity = created->entity();
  uint64_t count = 0;
  if (!first->find(entity).is_valid() ||
      gneiss::test_support::assign_value(first->entity_count(), count) !=
          gneiss::error::success ||
      count != 1U) {
    return 3;
  }
  gneiss::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::math::camera_settings queried_camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::entity_id active_camera = gneiss::entity_id{};
  if (gneiss::get_active_camera(*first, active_camera) !=
          gneiss::error::not_ready ||
      active_camera != gneiss::entity_id{} ||
      first->find(entity).add_component<gneiss::camera_component>(
          *gneiss::camera_component::create(camera)) != gneiss::error::success ||
      gneiss::test_support::read_component_value(
          first->find(entity).try_component<gneiss::camera_component>(), queried_camera) !=
          gneiss::error::success ||
      queried_camera.near != camera.near ||
      gneiss::set_active_camera(*second, entity) != gneiss::error::invalid_handle ||
      gneiss::set_active_camera(*first, entity) != gneiss::error::success ||
      gneiss::get_active_camera(*first, active_camera) != gneiss::error::success ||
      active_camera != entity) {
    return 4;
  }
  if (first->find(entity).remove_component<gneiss::camera_component>() !=
          gneiss::error::success ||
      first->find(entity).remove_component<gneiss::camera_component>() !=
          gneiss::error::not_found ||
      gneiss::get_active_camera(*first, active_camera) !=
          gneiss::error::not_ready ||
      gneiss::set_active_camera(*first, entity) != gneiss::error::not_ready ||
      first->find(entity).add_component<gneiss::camera_component>(
          *gneiss::camera_component::create(camera)) != gneiss::error::success ||
      gneiss::set_active_camera(*first, entity) != gneiss::error::success) {
    return 5;
  }
  const auto object = first->find(entity);
  queried_camera.near = 0.25F;
  if (object.add_component<gneiss::camera_component>(
          *gneiss::camera_component::create(queried_camera)) !=
          gneiss::error::invalid_state ||
      !object.try_component<gneiss::camera_component>() ||
      object.try_component<gneiss::camera_component>()->settings().near != camera.near)
    return 36;
  auto* borrowed = object.try_component<gneiss::camera_component>();
  if (!borrowed)
    return 31;
  auto captured = *borrowed;
  auto settings = captured.settings();
  settings.near = 0.5F;
  if (captured.set_settings(settings).failed() || borrowed->settings().near != camera.near)
    return 32;
  settings.near = -1;
  if (borrowed->set_settings(settings) != gneiss::error::invalid_argument ||
      borrowed->settings().near != camera.near ||
      gneiss::camera_component::create(settings).has_value())
    return 37;
  if (second->find(entity).is_valid())
    return 33;
  gneiss::error thread_status;
  std::thread worker(
      [&] { thread_status = object.add_component<gneiss::camera_component>(captured); });
  worker.join();
  if (thread_status != gneiss::error::invalid_state)
    return 34;
  if (second->find(entity).destroy() != gneiss::error::invalid_handle ||
      first->find(entity).destroy() != gneiss::error::success ||
      first->find(entity).destroy() != gneiss::error::invalid_handle) {
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
