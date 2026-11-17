// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/function/scene/game_object.hpp>
#include <gneiss/engine/assert.hpp>
#include "function/scene/detail/world_component_access.hpp"
#include "function/scene/detail/world_impl.hpp"
#include <gneiss/engine/function/scene/world.hpp>
#include <string>
#include <utility>

namespace gneiss {
std::vector<game_object> game_object::children() const {
  GNEISS_ASSERT(is_valid(), "GameObject 查询要求有效句柄及所属线程");
  auto state = detail::world_component_access::get(*owner_);
  const auto& ids = (*state)->get<detail::object_state>(entity_.get())->children;
  std::vector<game_object> result;
  result.reserve(ids.size());
  for (auto id : ids) {
    auto child = owner_->find(id);
    GNEISS_ASSERT(child.is_valid(), "对象层级不得包含失效子对象");
    result.push_back(child);
  }
  return result;
}
bool game_object::is_valid() const noexcept {
  return owner_ && owner_->object(entity_).has_value();
}
std::string_view game_object::name() const {
  GNEISS_DEBUG_ASSERT(is_valid(), "GameObject 借用句柄必须有效");
  auto state = detail::world_component_access::get(*owner_);
  return (*state)->get<detail::object_state>(entity_.get())->name;
}
math::transform game_object::local_transform() const {
  GNEISS_ASSERT(is_valid(), "GameObject 查询要求有效句柄及所属线程");
  auto state = detail::world_component_access::get(*owner_);
  return (*state)->get<detail::object_state>(entity_.get())->local;
}
scene_instance game_object::scene() const {
  GNEISS_ASSERT(is_valid(), "GameObject 查询要求有效句柄及所属线程");
  auto state = detail::world_component_access::get(*owner_);
  auto scene = scene_instance::borrow(*owner_, (*state)->scene_of(entity_));
  GNEISS_ASSERT(scene.has_value(), "GameObject 必须属于存活场景");
  return *scene;
}
math::matrix4 game_object::world_matrix() const {
  GNEISS_ASSERT(is_valid(), "GameObject 查询要求有效句柄及所属线程");
  auto state = detail::world_component_access::get(*owner_);
  const auto* object = (*state)->get<detail::object_state>(entity_.get());
  auto matrix = math::model_matrix(object->local);
  while (object->parent) {
    object = (*state)->get<detail::object_state>(object->parent.get());
    GNEISS_ASSERT(object, "对象层级不得包含失效父对象");
    matrix = math::model_matrix(object->local) * matrix;
  }
  return matrix;
}
result<math::transform> game_object::world_transform() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* object = (*state)->get<detail::object_state>(entity_.get());
  if (!object)
    return error::invalid_handle;
  auto transform = object->local;
  while (object->parent) {
    object = (*state)->get<detail::object_state>(object->parent.get());
    if (!object)
      return error::invalid_handle;
    transform = math::compose_transform(object->local, transform);
  }
  return transform;
}
error game_object::set_local_transform(const math::transform& transform) const {
  GNEISS_ASSERT(is_valid(), "GameObject 修改要求有效句柄及所属线程");
  auto state = detail::world_component_access::get(*owner_);
  auto* object = (*state)->get<detail::object_state>(entity_.get());
  if (!math::is_valid_transform(transform))
    return error::invalid_argument;
  object->local = transform;
  (*state)->mark_transform_changed(entity_);
  return error::success;
}
void game_object::set_name(std::string_view name) const {
  GNEISS_ASSERT(is_valid(), "GameObject 修改要求有效句柄及所属线程");
  auto state = detail::world_component_access::get(*owner_);
  auto* object = (*state)->get<detail::object_state>(entity_.get());
  object->name = std::string(name);
}
game_object game_object::parent() const {
  GNEISS_ASSERT(is_valid(), "GameObject 查询要求有效句柄及所属线程");
  auto state = detail::world_component_access::get(*owner_);
  const auto* object = (*state)->get<detail::object_state>(entity_.get());
  if (!object->parent)
    return {};
  auto parent = owner_->find(object->parent);
  GNEISS_ASSERT(parent.is_valid(), "对象层级不得包含失效父对象");
  return parent;
}
error game_object::reparent(game_object parent) const {
  GNEISS_ASSERT(is_valid(), "GameObject 修改要求有效句柄及所属线程");
  if (parent.owner_ && parent.owner_ != owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  auto* object = (*state)->get<detail::object_state>(entity_.get());
  auto* target = (*state)->get<detail::object_state>(parent.entity_.get());
  if (parent.entity_ && !target)
    return error::invalid_handle;
  if (target && object->scene != target->scene)
    return error::invalid_argument;
  for (auto ancestor = parent.entity_; ancestor;) {
    if (ancestor == entity_)
      return error::invalid_argument;
    const auto* ancestor_object = (*state)->get<detail::object_state>(ancestor.get());
    GNEISS_ASSERT(ancestor_object, "对象层级不得包含失效父对象");
    ancestor = ancestor_object->parent;
  }
  if (object->parent == parent.entity_)
    return error::success;
  // 先完成可能分配的操作，再修改旧父子关系。
  if (target)
    target->children.push_back(entity_);
  (*state)->detach(entity_);
  object->parent = parent.entity_;
  (*state)->mark_transform_changed(entity_);
  return error::success;
}
error game_object::destroy() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  if (!(*state)->get<detail::object_state>(entity_.get()))
    return error::invalid_handle;
  return (*state)->destroy_entity(entity_.get()) ? error::success : error::invalid_handle;
}
}
