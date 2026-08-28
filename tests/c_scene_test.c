// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/scene.h>

int main(void) {
  const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_entity_id entity = GNEISS_NULL_ENTITY_ID;
  gneiss_scene_node_id root = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id child = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id duplicate = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_scene_node_id parent = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_transform root_transform = GNEISS_TRANSFORM_IDENTITY;
  gneiss_transform child_transform = GNEISS_TRANSFORM_IDENTITY;
  gneiss_transform world_transform = GNEISS_TRANSFORM_IDENTITY;
  gneiss_entity_id attached = GNEISS_NULL_ENTITY_ID;

  if (gneiss_world_create(&desc, &world) != GNEISS_SUCCESS ||
      gneiss_world_entity_create(world, &entity) != GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, GNEISS_NULL_SCENE_NODE_ID, GNEISS_NULL_ENTITY_ID, &root) !=
          GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, root, entity, &child) != GNEISS_SUCCESS) {
    return 1;
  }

  if (gneiss_scene_node_create(world, root, entity, &duplicate) != GNEISS_ERROR_INVALID_STATE) {
    return 2;
  }

  root_transform.translation[0] = 2.0F;
  child_transform.translation[1] = 3.0F;
  if (gneiss_scene_node_set_local_transform(world, root, &root_transform) != GNEISS_SUCCESS ||
      gneiss_scene_node_set_local_transform(world, child, &child_transform) != GNEISS_SUCCESS ||
      gneiss_scene_node_get_parent(world, child, &parent) != GNEISS_SUCCESS || parent != root ||
      gneiss_scene_node_get_parent(world, root, &parent) != GNEISS_SUCCESS ||
      parent != GNEISS_NULL_SCENE_NODE_ID ||
      gneiss_scene_node_get_world_transform(world, child, &world_transform) != GNEISS_SUCCESS ||
      world_transform.translation[0] != 2.0F || world_transform.translation[1] != 3.0F) {
    return 3;
  }

  if (gneiss_scene_node_reparent(world, root, child) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_world_entity_destroy(world, entity) != GNEISS_SUCCESS ||
      gneiss_scene_node_get_entity(world, child, &attached) != GNEISS_SUCCESS ||
      attached != GNEISS_NULL_ENTITY_ID) {
    return 4;
  }

  if (gneiss_scene_node_destroy(world, root) != GNEISS_SUCCESS ||
      gneiss_scene_node_destroy(world, child) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_destroy(world) != GNEISS_SUCCESS) {
    return 5;
  }
  return 0;
}
