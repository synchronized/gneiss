// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.h>

int main(void) {
  gneiss_world first = GNEISS_NULL_WORLD;
  gneiss_world second = GNEISS_NULL_WORLD;
  const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
  if (gneiss_world_create(&desc, &first) != GNEISS_SUCCESS ||
      gneiss_world_create(&desc, &second) != GNEISS_SUCCESS) {
    return 1;
  }

  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  if (gneiss_world_entity_create(first, &entity) != GNEISS_SUCCESS ||
      entity == GNEISS_NULL_ENTITY_ID) {
    return 2;
  }
  uint8_t is_alive = 0;
  uint64_t count = 0;
  if (gneiss_world_entity_is_alive(first, entity, &is_alive) != GNEISS_SUCCESS || is_alive != 1U ||
      gneiss_world_entity_count(first, &count) != GNEISS_SUCCESS || count != 1U) {
    return 3;
  }
  gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  gneiss_camera_desc queried_camera = GNEISS_CAMERA_DESC_INIT;
  gneiss_entity_id active_camera = GNEISS_NULL_ENTITY_ID;
  camera.struct_size = 0U;
  if (gneiss_world_entity_configure_camera(first, entity, &camera) !=
      GNEISS_ERROR_INVALID_ARGUMENT) {
    return 4;
  }
  camera = (gneiss_camera_desc)GNEISS_CAMERA_DESC_INIT;
  queried_camera.struct_size = 0U;
  if (gneiss_world_entity_get_camera(first, entity, &queried_camera) !=
      GNEISS_ERROR_INVALID_ARGUMENT) {
    return 4;
  }
  queried_camera = (gneiss_camera_desc)GNEISS_CAMERA_DESC_INIT;
  if (gneiss_world_get_active_camera(first, &active_camera) != GNEISS_ERROR_NOT_READY ||
      active_camera != GNEISS_NULL_ENTITY_ID ||
      gneiss_world_entity_configure_camera(first, entity, &camera) != GNEISS_SUCCESS ||
      gneiss_world_entity_get_camera(first, entity, &queried_camera) != GNEISS_SUCCESS ||
      queried_camera.near_plane != camera.near_plane ||
      gneiss_world_set_active_camera(second, entity) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_set_active_camera(first, entity) != GNEISS_SUCCESS ||
      gneiss_world_get_active_camera(first, &active_camera) != GNEISS_SUCCESS ||
      active_camera != entity) {
    return 4;
  }
  if (gneiss_world_entity_remove_camera(first, entity) != GNEISS_SUCCESS ||
      gneiss_world_entity_remove_camera(first, entity) != GNEISS_ERROR_NOT_FOUND ||
      gneiss_world_get_active_camera(first, &active_camera) != GNEISS_ERROR_NOT_READY ||
      gneiss_world_set_active_camera(first, entity) != GNEISS_ERROR_NOT_READY ||
      gneiss_world_entity_configure_camera(first, entity, &camera) != GNEISS_SUCCESS ||
      gneiss_world_set_active_camera(first, entity) != GNEISS_SUCCESS) {
    return 5;
  }
  if (gneiss_world_entity_destroy(second, entity) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_entity_destroy(first, entity) != GNEISS_SUCCESS ||
      gneiss_world_entity_destroy(first, entity) != GNEISS_ERROR_INVALID_HANDLE) {
    return 6;
  }
  if (gneiss_world_entity_is_alive(first, entity, &is_alive) != GNEISS_SUCCESS || is_alive != 0U ||
      gneiss_world_get_active_camera(first, &active_camera) != GNEISS_ERROR_NOT_READY) {
    return 7;
  }
  if (gneiss_world_destroy(first) != GNEISS_SUCCESS ||
      gneiss_world_destroy(first) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_destroy(second) != GNEISS_SUCCESS) {
    return 8;
  }
  return 0;
}
