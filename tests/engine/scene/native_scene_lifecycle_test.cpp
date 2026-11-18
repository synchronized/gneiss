// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <thread>

int main(void) {
  if (gneiss::initialize_meta().failed())
    return 21;
  auto created_world = gneiss::create_native();
  if (!created_world)
    return 1;
  auto world = std::move(*created_world);
  gneiss::math::transform root_transform;
  gneiss::math::transform child_transform;
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
  const auto entity = child_object->entity();

  root_transform.translation[0] = 2.0F;
  child_transform.translation[1] = 3.0F;
  if (root_object->set_local_transform(root_transform) != gneiss::error::success ||
      child_object->set_local_transform(child_transform) != gneiss::error::success ||
      child_object->parent() != *root_object || root_object->parent().is_valid() ||
      child_object->world_transform().translation[0] != 2.0F ||
      child_object->world_transform().translation[1] != 3.0F) {
    return 3;
  }

  if (root_object->reparent(*child_object) != gneiss::error::invalid_argument)
    return 4;
  world->find(entity).destroy();
  const auto destroyed = world->object(entity);
  if (destroyed || destroyed.error() != gneiss::error::invalid_handle || child_object->is_valid())
    return 4;

  root_object->destroy();
  if (child_object->is_valid()) {
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
      object->reparent(*foreign) != error::invalid_handle)
    return 6;
  auto rejected = object_scene->create_object({"cross", {}}, *foreign);
  if (rejected || rejected.error() != error::invalid_handle)
    return 7;
  auto descendant = object_scene->create_object({"child", {}}, *object);
  if (!descendant || object->reparent(*descendant) != error::invalid_argument)
    return 8;
  auto expired_parent = object_scene->create_object({"temporary", {}});
  if (!expired_parent)
    return 22;
  expired_parent->destroy();
  if (descendant->reparent(*expired_parent) != error::invalid_handle ||
      descendant->parent() != *object)
    return 22;
  auto borrowed_parent = descendant->parent();
  auto no_parent = object->parent();
  if (borrowed_parent != *object || no_parent.is_valid())
    return 9;
  math::transform local;
  local.translation.x = 4;
  if (object->set_local_transform(local).failed())
    return 10;
  auto transformed = descendant->world_transform();
  if (transformed.translation.x != 4)
    return 11;
  error foreign_thread;
  std::thread worker([&] { foreign_thread = world->object(object->entity()).error(); });
  worker.join();
  if (foreign_thread != error::invalid_state)
    return 16;
  auto copy = *object;
  object->destroy();
  if (copy.is_valid() || descendant->is_valid())
    return 12;
  auto replacement = object_scene->create_object({"replacement", {}});
  if (!replacement || replacement->entity() == copy.entity() || copy.is_valid())
    return 13;
  auto membership = replacement->scene();
  if (membership.id() != object_scene->id())
    return 17;
  auto scene_copy = *object_scene;
  object_scene->unload();
  if (scene_copy.is_valid() || replacement->is_valid())
    return 18;
  if (scene_copy.create_object({"expired", {}}).error() != error::invalid_handle)
    return 19;
  return 0;
}
