// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"
#include "support/value_result.hpp"

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
      first->entity_configure_camera(entity, camera) != gneiss::engine::error::success ||
      first->entity_get_camera(entity, queried_camera) != gneiss::engine::error::success ||
      queried_camera.near != camera.near ||
      second->set_active_camera(entity) != gneiss::engine::error::invalid_handle ||
      first->set_active_camera(entity) != gneiss::engine::error::success ||
      first->get_active_camera(active_camera) != gneiss::engine::error::success ||
      active_camera != entity) {
    return 4;
  }
  if (first->entity_remove_camera(entity) != gneiss::engine::error::success ||
      first->entity_remove_camera(entity) != gneiss::engine::error::not_found ||
      first->get_active_camera(active_camera) != gneiss::engine::error::not_ready ||
      first->set_active_camera(entity) != gneiss::engine::error::not_ready ||
      first->entity_configure_camera(entity, camera) != gneiss::engine::error::success ||
      first->set_active_camera(entity) != gneiss::engine::error::success) {
    return 5;
  }
  if (second->entity_destroy(entity) != gneiss::engine::error::invalid_handle ||
      first->entity_destroy(entity) != gneiss::engine::error::success ||
      first->entity_destroy(entity) != gneiss::engine::error::invalid_handle) {
    return 6;
  }
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
