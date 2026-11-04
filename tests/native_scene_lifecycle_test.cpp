// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"
#include "support/value_result.hpp"

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

  if (gneiss::test_support::assign_value(gneiss::engine::create_native(), world) !=
          gneiss::engine::error::success ||
      world->entity_create(entity) != gneiss::engine::error::success ||
      world->node_create(gneiss::scene_node_id{}, gneiss::entity_id{}, root) !=
          gneiss::engine::error::success ||
      world->node_create(root, entity, child) != gneiss::engine::error::success) {
    return 1;
  }

  if (world->node_create(root, entity, duplicate) != gneiss::engine::error::invalid_state) {
    return 2;
  }

  root_transform.translation[0] = 2.0F;
  child_transform.translation[1] = 3.0F;
  if (world->node_set_local_transform(root, root_transform) != gneiss::engine::error::success ||
      world->node_set_local_transform(child, child_transform) != gneiss::engine::error::success ||
      world->node_get_parent(child, parent) != gneiss::engine::error::success || parent != root ||
      world->node_get_parent(root, parent) != gneiss::engine::error::success ||
      parent != gneiss::scene_node_id{} ||
      world->node_get_world_transform(child, world_transform) != gneiss::engine::error::success ||
      world_transform.translation[0] != 2.0F || world_transform.translation[1] != 3.0F) {
    return 3;
  }

  if (world->node_reparent(root, child) != gneiss::engine::error::invalid_argument ||
      world->entity_destroy(entity) != gneiss::engine::error::success ||
      world->node_get_entity(child, attached) != gneiss::engine::error::success ||
      attached != gneiss::entity_id{}) {
    return 4;
  }

  if (world->node_destroy(root) != gneiss::engine::error::success ||
      world->node_destroy(child) != gneiss::engine::error::invalid_handle) {
    return 5;
  }
  return 0;
}
