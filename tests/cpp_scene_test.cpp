// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/world.hpp>

#include <limits>
#include <type_traits>

static_assert(!std::is_same_v<gneiss::transform, gneiss_transform>);
static_assert(gneiss::transform{}.translation[0] == 0.0F);
static_assert(gneiss::transform{}.rotation[3] == 1.0F);
static_assert(gneiss::transform{}.scale[2] == 1.0F);

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
  gneiss::transform local{};
  local.translation[2] = 4.0F;
  gneiss::transform combined{};
  if (world.set_local_transform(child, local) != gneiss::result::success ||
      world.get_world_transform(child, combined) != gneiss::result::success ||
      combined.translation[2] != 4.0F) {
    return 2;
  }
  gneiss::scene_node_id parent = root;
  gneiss::transform queried{};
  if (world.reparent_scene_node(root, child) != gneiss::result::invalid_argument ||
      world.reparent_scene_node(child, {}).failed() || world.get_parent(child, parent).failed() ||
      parent.is_valid() || world.set_local_transform(entity, local).failed() ||
      world.get_local_transform(entity, queried).failed() || queried.translation[2] != 4.0F ||
      world.reparent_scene_node(child, root).failed()) {
    return 3;
  }
  // 父子组合与 C 边界逐字段复制；失败不能覆盖原输出或已提交变换。
  gneiss::transform root_transform{};
  root_transform.translation[0] = 3.0F;
  root_transform.scale[2] = 2.0F;
  if (world.set_local_transform(root, root_transform).failed() ||
      world.get_world_transform(child, combined).failed() || combined.translation[0] != 3.0F ||
      combined.translation[2] != 8.0F) {
    return 6;
  }
  auto invalid = local;
  invalid.scale[1] = 0.0F;
  if (world.set_local_transform(child, invalid) != gneiss::result::invalid_argument ||
      world.get_local_transform(child, queried).failed() || queried.scale != local.scale) {
    return 7;
  }
  invalid = local;
  invalid.translation[0] = std::numeric_limits<float>::quiet_NaN();
  if (world.set_local_transform(entity, invalid) != gneiss::result::invalid_argument ||
      world.get_local_transform(gneiss::entity_id{}, queried) != gneiss::result::invalid_handle ||
      queried.translation != local.translation) {
    return 8;
  }
  const auto roundtrip = gneiss::from_native(gneiss::to_native(root_transform));
  if (roundtrip.translation != root_transform.translation ||
      roundtrip.rotation != root_transform.rotation || roundtrip.scale != root_transform.scale) {
    return 9;
  }
  bool alive = false;
  if (world.destroy_scene_node(root).failed() ||
      world.destroy_scene_node(child) != gneiss::result::invalid_handle ||
      world.get_world_transform(child, combined) != gneiss::result::invalid_handle ||
      combined.translation[2] != 8.0F || world.is_alive(entity, alive).failed() || !alive) {
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
