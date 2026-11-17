// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include "support/value_result.hpp"
#include <thread>

int main(void) {
  if (gneiss::initialize_meta().failed())
    return 21;
  gneiss::world_owner world;

  gneiss::entity_id entity = gneiss::entity_id{};
  gneiss::entity_id root = gneiss::entity_id{};
  gneiss::entity_id child = gneiss::entity_id{};
  gneiss::math::transform root_transform;
  gneiss::math::transform child_transform;
  gneiss::math::transform world_transform;
  gneiss::entity_id attached = gneiss::entity_id{};

  if (gneiss::test_support::assign_value(gneiss::create_native(), world).failed())
    return 1;
  auto object_scene = world->create_scene();
  if (!object_scene)
    return 1;
  auto root_object = object_scene->create_object({"root", {}});
  if (!root_object)
    return 1;
  auto child_object = object_scene->create_object({root_object->name()}, *root_object);
  if (!child_object || child_object->name() != "root")
    return 1;
  child_object->set_name("child");
  root = gneiss::entity_id{root_object->entity().get()};
  child = gneiss::entity_id{child_object->entity().get()};
  entity = child_object->entity();

  root_transform.translation[0] = 2.0F;
  child_transform.translation[1] = 3.0F;
  if (world->find(gneiss::entity_id{(root).get()}).set_local_transform(root_transform) !=
          gneiss::error::success ||
      world->find(gneiss::entity_id{(child).get()}).set_local_transform(child_transform) !=
          gneiss::error::success ||
      world->find(gneiss::entity_id{(child).get()}).parent().entity() != root ||
      world->find(gneiss::entity_id{(root).get()}).parent().entity() != gneiss::entity_id{} ||
      gneiss::test_support::assign_value(
          world->find(gneiss::entity_id{(child).get()}).world_transform(), world_transform) !=
          gneiss::error::success ||
      world_transform.translation[0] != 2.0F || world_transform.translation[1] != 3.0F) {
    return 3;
  }

  if (world->find(gneiss::entity_id{(root).get()})
              .reparent(world->find(gneiss::entity_id{(child).get()})) !=
          gneiss::error::invalid_argument ||
      world->find(entity).destroy() != gneiss::error::success ||
      gneiss::test_support::assign_value(world->object(gneiss::entity_id{(child).get()}),
                                         attached) != gneiss::error::invalid_handle ||
      attached != gneiss::entity_id{}) {
    return 4;
  }

  if (world->find(gneiss::entity_id{(root).get()}).destroy() != gneiss::error::success ||
      world->find(gneiss::entity_id{(child).get()}).destroy() !=
          gneiss::error::invalid_handle) {
    return 5;
  }
  // 借用对象不拥有实体；空值、跨 World、generation 与子树销毁使用同一规则。
  using namespace gneiss;
  auto another = create_native();
  auto object = object_scene->create_object({"root", {}});
  if (!another)
    return 6;
  auto foreign_scene = (*another)->create_scene();
  if (!foreign_scene)
    return 6;
  auto foreign = foreign_scene->create_object({"foreign", {}});
  if (!object || !foreign || !object->is_valid() || game_object{}.is_valid() ||
      game_object{}.destroy() != error::invalid_handle ||
      object->reparent(*foreign) != error::invalid_handle)
    return 6;
  auto rejected = object_scene->create_object({"cross", {}}, *foreign);
  if (rejected || rejected.error() != error::invalid_handle)
    return 7;
  auto descendant = object_scene->create_object({"child", {}}, *object);
  if (!descendant || object->reparent(*descendant) != error::invalid_argument)
    return 8;
  auto borrowed_parent = descendant->parent();
  auto no_parent = object->parent();
  if (borrowed_parent != *object || no_parent.is_valid())
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
  auto replacement = object_scene->create_object({"replacement", {}});
  if (!replacement || replacement->entity() == copy.entity() || copy.is_valid())
    return 13;
  auto membership = replacement->scene();
  if (membership.id() != object_scene->id())
    return 17;
  auto scene_copy = *object_scene;
  if (object_scene->unload().failed() || scene_copy.is_valid() || replacement->is_valid())
    return 18;
  if (scene_copy.create_object({"expired", {}}).error() != error::invalid_handle)
    return 19;
  return 0;
}
