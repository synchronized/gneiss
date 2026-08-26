// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/world.hpp>

int main() {
  gneiss::world world;
  gneiss::entity_id entity;
  gneiss::scene_node_id root;
  gneiss::scene_node_id child;
  if (gneiss::world::create(world) != gneiss::result::success ||
      world.create_entity(entity) != gneiss::result::success ||
      world.create_scene_node(gneiss::null_scene_node_id, gneiss::null_entity_id, root) !=
          gneiss::result::success ||
      world.create_scene_node(root, entity, child) != gneiss::result::success) {
    return 1;
  }
  gneiss::transform local = GNEISS_TRANSFORM_IDENTITY;
  local.translation[2] = 4.0F;
  gneiss::transform combined = GNEISS_TRANSFORM_IDENTITY;
  if (world.set_local_transform(child, local) != gneiss::result::success ||
      world.get_world_transform(child, combined) != gneiss::result::success ||
      combined.translation[2] != 4.0F) {
    return 2;
  }
  return world.destroy_scene_node(root) == gneiss::result::success ? 0 : 3;
}
