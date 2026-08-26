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
  if (gneiss_world_entity_destroy(second, entity) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_entity_destroy(first, entity) != GNEISS_SUCCESS ||
      gneiss_world_entity_destroy(first, entity) != GNEISS_ERROR_INVALID_HANDLE) {
    return 4;
  }
  if (gneiss_world_entity_is_alive(first, entity, &is_alive) != GNEISS_SUCCESS || is_alive != 0U) {
    return 5;
  }
  if (gneiss_world_destroy(first) != GNEISS_SUCCESS ||
      gneiss_world_destroy(first) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_destroy(second) != GNEISS_SUCCESS) {
    return 6;
  }
  return 0;
}
