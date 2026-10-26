// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"

int main(void) {
  gneiss::engine::world_owner first;

  gneiss::engine::world_owner second;

  if (gneiss::engine::create_native(first) != gneiss::engine::result::success ||
      gneiss::engine::create_native(second) != gneiss::engine::result::success) {
    return 1;
  }

  gneiss::entity_id entity = gneiss::entity_id{};
  if (first->entity_create(entity) != gneiss::engine::result::success ||
      entity == gneiss::entity_id{}) {
    return 2;
  }
  bool is_alive = false;
  uint64_t count = 0;
  if (first->entity_is_alive(entity, is_alive) != gneiss::engine::result::success ||
      is_alive != 1U || first->entity_count(count) != gneiss::engine::result::success ||
      count != 1U) {
    return 3;
  }
  gneiss::engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::engine::camera_settings queried_camera{1.04719755F, 0.1F, 1000.0F};
  gneiss::entity_id active_camera = gneiss::entity_id{};
  if (first->get_active_camera(active_camera) != gneiss::engine::result::not_ready ||
      active_camera != gneiss::entity_id{} ||
      first->entity_configure_camera(entity, camera) != gneiss::engine::result::success ||
      first->entity_get_camera(entity, queried_camera) != gneiss::engine::result::success ||
      queried_camera.near != camera.near ||
      second->set_active_camera(entity) != gneiss::engine::result::invalid_handle ||
      first->set_active_camera(entity) != gneiss::engine::result::success ||
      first->get_active_camera(active_camera) != gneiss::engine::result::success ||
      active_camera != entity) {
    return 4;
  }
  if (first->entity_remove_camera(entity) != gneiss::engine::result::success ||
      first->entity_remove_camera(entity) != gneiss::engine::result::not_found ||
      first->get_active_camera(active_camera) != gneiss::engine::result::not_ready ||
      first->set_active_camera(entity) != gneiss::engine::result::not_ready ||
      first->entity_configure_camera(entity, camera) != gneiss::engine::result::success ||
      first->set_active_camera(entity) != gneiss::engine::result::success) {
    return 5;
  }
  if (second->entity_destroy(entity) != gneiss::engine::result::invalid_handle ||
      first->entity_destroy(entity) != gneiss::engine::result::success ||
      first->entity_destroy(entity) != gneiss::engine::result::invalid_handle) {
    return 6;
  }
  if (first->entity_is_alive(entity, is_alive) != gneiss::engine::result::success ||
      is_alive != 0U ||
      first->get_active_camera(active_camera) != gneiss::engine::result::not_ready) {
    return 7;
  }
  first.reset();
  second.reset();
  return 0;
}
