// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"

int main(void) {
  gneiss::engine::world_owner world;

  gneiss::entity_id entity = gneiss::entity_id{};
  gneiss::scene_node_id root = gneiss::scene_node_id{};
  gneiss::scene_node_id child = gneiss::scene_node_id{};
  gneiss::scene_node_id duplicate = gneiss::scene_node_id{};
  gneiss::scene_node_id parent = gneiss::scene_node_id{};
  gneiss::engine::math::transform root_transform;
  gneiss::engine::math::transform child_transform;
  gneiss::engine::math::transform world_transform;
  gneiss::entity_id attached = gneiss::entity_id{};

  if (gneiss::engine::create_native(world) != gneiss::engine::result::success ||
      world->entity_create(entity) != gneiss::engine::result::success ||
      world->node_create(gneiss::scene_node_id{}, gneiss::entity_id{}, root) !=
          gneiss::engine::result::success ||
      world->node_create(root, entity, child) != gneiss::engine::result::success) {
    return 1;
  }

  if (world->node_create(root, entity, duplicate) != gneiss::engine::result::invalid_state) {
    return 2;
  }

  root_transform.translation[0] = 2.0F;
  child_transform.translation[1] = 3.0F;
  if (world->node_set_local_transform(root, root_transform) != gneiss::engine::result::success ||
      world->node_set_local_transform(child, child_transform) != gneiss::engine::result::success ||
      world->node_get_parent(child, parent) != gneiss::engine::result::success || parent != root ||
      world->node_get_parent(root, parent) != gneiss::engine::result::success ||
      parent != gneiss::scene_node_id{} ||
      world->node_get_world_transform(child, world_transform) != gneiss::engine::result::success ||
      world_transform.translation[0] != 2.0F || world_transform.translation[1] != 3.0F) {
    return 3;
  }

  if (world->node_reparent(root, child) != gneiss::engine::result::invalid_argument ||
      world->entity_destroy(entity) != gneiss::engine::result::success ||
      world->node_get_entity(child, attached) != gneiss::engine::result::success ||
      attached != gneiss::entity_id{}) {
    return 4;
  }

  if (world->node_destroy(root) != gneiss::engine::result::success ||
      world->node_destroy(child) != gneiss::engine::result::invalid_handle) {
    return 5;
  }
  return 0;
}
