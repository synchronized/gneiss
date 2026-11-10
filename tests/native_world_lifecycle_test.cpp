// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/components.hpp"
#include "engine/function/scene/world.hpp"
#include "support/value_result.hpp"
#include <thread>

int main(void) {
  gneiss::engine::world_owner first;

  gneiss::engine::world_owner second;

  if (gneiss::test_support::assign_value(gneiss::engine::create_native(), first) !=
          gneiss::engine::error::success ||
      gneiss::test_support::assign_value(gneiss::engine::create_native(), second) !=
          gneiss::engine::error::success) {
    return 1;
  }

  auto empty_count = first->entity_count();
  auto missing = first->entity_is_alive({});
  if (!empty_count || *empty_count != 0U || !missing || *missing)
    return 30;
  gneiss::entity_id entity = gneiss::entity_id{};
  if (first->entity_create(entity) != gneiss::engine::error::success ||
      entity == gneiss::entity_id{}) {
    return 2;
  }
  bool is_alive = false;
  uint64_t count = 0;
  if (gneiss::test_support::assign_value(first->entity_is_alive(entity), is_alive) !=
          gneiss::engine::error::success ||
      is_alive != 1U ||
      gneiss::test_support::assign_value(first->entity_count(), count) !=
          gneiss::engine::error::success ||
      count != 1U) {
    return 3;
  }
  gneiss::engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::engine::math::camera_settings queried_camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::entity_id active_camera = gneiss::entity_id{};
  if (first->get_active_camera(active_camera) != gneiss::engine::error::not_ready ||
      active_camera != gneiss::entity_id{} ||
      first->find(entity).set_component(gneiss::engine::camera_component{camera}) !=
          gneiss::engine::error::success ||
      gneiss::test_support::read_component_value(
          first->find(entity).read_component<gneiss::engine::camera_component>(), queried_camera) !=
          gneiss::engine::error::success ||
      queried_camera.near != camera.near ||
      second->set_active_camera(entity) != gneiss::engine::error::invalid_handle ||
      first->set_active_camera(entity) != gneiss::engine::error::success ||
      first->get_active_camera(active_camera) != gneiss::engine::error::success ||
      active_camera != entity) {
    return 4;
  }
  if (first->find(entity).remove_component<gneiss::engine::camera_component>() !=
          gneiss::engine::error::success ||
      first->find(entity).remove_component<gneiss::engine::camera_component>() !=
          gneiss::engine::error::not_found ||
      first->get_active_camera(active_camera) != gneiss::engine::error::not_ready ||
      first->set_active_camera(entity) != gneiss::engine::error::not_ready ||
      first->find(entity).set_component(gneiss::engine::camera_component{camera}) !=
          gneiss::engine::error::success ||
      first->set_active_camera(entity) != gneiss::engine::error::success) {
    return 5;
  }
  const auto object = first->find(entity);
  auto captured = object.read_component<gneiss::engine::camera_component>();
  if (!captured)
    return 31;
  captured->value.near = 0.5F;
  auto unchanged = object.read_component<gneiss::engine::camera_component>();
  if (!unchanged || unchanged->value.near != camera.near)
    return 32;
  auto foreign = second->find(entity).read_component<gneiss::engine::camera_component>();
  if (foreign || foreign.error() != gneiss::engine::error::invalid_handle)
    return 33;
  gneiss::engine::error thread_status;
  std::thread worker([&] { thread_status = object.set_component(*captured); });
  worker.join();
  if (thread_status != gneiss::engine::error::invalid_state)
    return 34;
  if (second->entity_destroy(entity) != gneiss::engine::error::invalid_handle ||
      first->entity_destroy(entity) != gneiss::engine::error::success ||
      first->entity_destroy(entity) != gneiss::engine::error::invalid_handle) {
    return 6;
  }
  auto expired = object.read_component<gneiss::engine::camera_component>();
  if (expired || expired.error() != gneiss::engine::error::invalid_handle ||
      object.remove_component<gneiss::engine::camera_component>() !=
          gneiss::engine::error::invalid_handle)
    return 35;
  if (gneiss::test_support::assign_value(first->entity_is_alive(entity), is_alive) !=
          gneiss::engine::error::success ||
      is_alive != 0U ||
      first->get_active_camera(active_camera) != gneiss::engine::error::not_ready) {
    return 7;
  }
  first.reset();
  second.reset();
  return 0;
}
