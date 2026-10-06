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
  gneiss::scene_node_id parent = root;
  gneiss::transform queried = GNEISS_TRANSFORM_IDENTITY;
  if (world.reparent_scene_node(root, child) != gneiss::result::invalid_argument ||
      world.reparent_scene_node(child, {}).failed() || world.get_parent(child, parent).failed() ||
      parent.is_valid() || world.set_local_transform(entity, local).failed() ||
      world.get_local_transform(entity, queried).failed() || queried.translation[2] != 4.0F ||
      world.reparent_scene_node(child, root).failed()) {
    return 3;
  }
  bool alive = false;
  if (world.destroy_scene_node(root).failed() ||
      world.destroy_scene_node(child) != gneiss::result::invalid_handle ||
      world.get_world_transform(child, combined) != gneiss::result::invalid_handle ||
      world.is_alive(entity, alive).failed() || !alive) {
    return 4;
  }
  // 节点只关联实体；递归删除节点不会销毁 World 拥有的实体，也不会复活旧节点 ID。
  gneiss::scene_node_id replacement;
  if (world.create_scene_node({}, entity, replacement).failed() || replacement == child ||
      world.get_local_transform(child, queried) != gneiss::result::invalid_handle) {
    return 5;
  }
  return 0;
}
