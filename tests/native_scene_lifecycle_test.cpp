// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/world.hpp"
#include "support/value_result.hpp"
#include <thread>

int main(void) {
  gneiss::engine::world_owner world;

  gneiss::entity_id entity = gneiss::entity_id{};
  gneiss::scene_node_id root = gneiss::scene_node_id{};
  gneiss::scene_node_id child = gneiss::scene_node_id{};
  gneiss::scene_node_id parent = gneiss::scene_node_id{};
  gneiss::engine::math::transform root_transform;
  gneiss::engine::math::transform child_transform;
  gneiss::engine::math::transform world_transform;
  gneiss::entity_id attached = gneiss::entity_id{};

  if (gneiss::test_support::assign_value(gneiss::engine::create_native(), world).failed())
    return 1;
  auto root_object = world->create_object({{}, "root"});
  if (!root_object)
    return 1;
  auto child_object = world->create_object({{}, "child"}, {}, *root_object);
  if (!child_object)
    return 1;
  root = gneiss::scene_node_id{root_object->entity().get()};
  child = gneiss::scene_node_id{child_object->entity().get()};
  entity = child_object->entity();

  root_transform.translation[0] = 2.0F;
  child_transform.translation[1] = 3.0F;
  if (world->find(gneiss::entity_id{(root).get()}).set_local_transform(root_transform) !=
          gneiss::engine::error::success ||
      world->find(gneiss::entity_id{(child).get()}).set_local_transform(child_transform) !=
          gneiss::engine::error::success ||
      gneiss::test_support::assign_value(world->find(gneiss::entity_id{(child).get()}).parent(),
                                         parent) != gneiss::engine::error::success ||
      parent != root ||
      gneiss::test_support::assign_value(world->find(gneiss::entity_id{(root).get()}).parent(),
                                         parent) != gneiss::engine::error::success ||
      parent != gneiss::scene_node_id{} ||
      gneiss::test_support::assign_value(
          world->find(gneiss::entity_id{(child).get()}).world_transform(), world_transform) !=
          gneiss::engine::error::success ||
      world_transform.translation[0] != 2.0F || world_transform.translation[1] != 3.0F) {
    return 3;
  }

  if (world->find(gneiss::entity_id{(root).get()})
              .reparent(world->find(gneiss::entity_id{(child).get()})) !=
          gneiss::engine::error::invalid_argument ||
      world->entity_destroy(entity) != gneiss::engine::error::success ||
      gneiss::test_support::assign_value(world->object(gneiss::entity_id{(child).get()}),
                                         attached) != gneiss::engine::error::invalid_handle ||
      attached != gneiss::entity_id{}) {
    return 4;
  }

  if (world->find(gneiss::entity_id{(root).get()}).destroy() != gneiss::engine::error::success ||
      world->find(gneiss::entity_id{(child).get()}).destroy() !=
          gneiss::engine::error::invalid_handle) {
    return 5;
  }
  // 借用对象不拥有实体；空值、跨 World、generation 与子树销毁使用同一规则。
  using namespace gneiss::engine;
  auto another = create_native();
  auto object = world->create_object({{}, "root"});
  auto foreign =
      another ? (*another)->create_object({{}, "foreign"}) : result<game_object>{error::internal};
  if (!object || !foreign || !object->is_valid() || game_object{}.is_valid() ||
      game_object{}.destroy() != error::invalid_handle ||
      object->reparent(*foreign) != error::invalid_handle)
    return 6;
  auto rejected = world->create_object({{}, "cross"}, {}, *foreign);
  if (rejected || rejected.error() != error::invalid_handle)
    return 7;
  auto descendant = world->create_object({{}, "child"}, {}, *object);
  if (!descendant || object->reparent(*descendant) != error::invalid_argument)
    return 8;
  auto borrowed_parent = descendant->parent();
  auto no_parent = object->parent();
  if (!borrowed_parent || *borrowed_parent != *object || !no_parent || no_parent->is_valid())
    return 9;
  math::transform local;
  local.translation.x = 4;
  if (object->set_local_transform(local).failed())
    return 10;
  auto transformed = descendant->world_transform();
  if (!transformed || transformed->translation.x != 4)
    return 11;
  error foreign_thread;
  std::thread worker([&] { foreign_thread = object->set_local_transform(local); });
  worker.join();
  if (foreign_thread != error::invalid_state)
    return 16;
  auto copy = *object;
  if (object->destroy().failed() || copy.is_valid() || descendant->is_valid() ||
      copy.destroy() != error::invalid_handle)
    return 12;
  auto replacement = world->create_object({{}, "replacement"});
  if (!replacement || replacement->entity() == copy.entity() || copy.is_valid())
    return 13;
  gneiss::entity_id pure;
  if (world->entity_create(pure).failed())
    return 14;
  auto not_object = world->object(pure);
  if (not_object || not_object.error() != error::not_found || world->entity_destroy(pure).failed())
    return 15;
  return 0;
}
