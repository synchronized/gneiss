// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/game_object.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/detail/world_impl.hpp"
#include "engine/function/scene/world.hpp"
#include <cassert>
#include <string>
#include <utility>

namespace gneiss {
result<std::vector<game_object>> game_object::children() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* object = (*state)->get<detail::object_state>(entity_.get());
  const auto* ids = object ? &object->children : nullptr;
  if (!ids)
    return error::invalid_handle;
  std::vector<game_object> result;
  result.reserve(ids->size());
  for (auto id : *ids)
    result.push_back(owner_->find(id));
  return result;
}
bool game_object::is_valid() const noexcept {
  return owner_ && owner_->object(entity_).has_value();
}
std::string_view game_object::name() const {
  assert(is_valid());
  auto state = detail::world_component_access::get(*owner_);
  return (*state)->get<detail::object_state>(entity_.get())->name;
}
result<math::transform> game_object::local_transform() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* object = (*state)->get<detail::object_state>(entity_.get());
  const auto* value = object ? &object->local : nullptr;
  return value ? result<math::transform>{*value} : result<math::transform>{error::invalid_handle};
}
result<scene_instance> game_object::scene() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return scene_instance::borrow(*owner_, (*state)->scene_of(entity_id{entity_.get()}));
}
result<math::matrix4> game_object::world_matrix() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* object = (*state)->get<detail::object_state>(entity_.get());
  if (!object)
    return error::invalid_handle;
  auto matrix = math::model_matrix(object->local);
  while (object->parent) {
    object = (*state)->get<detail::object_state>(object->parent.get());
    if (!object)
      return error::invalid_handle;
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
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  auto* object = (*state)->get<detail::object_state>(entity_.get());
  if (!object)
    return error::invalid_handle;
  if (!math::is_valid_transform(transform))
    return error::invalid_argument;
  object->local = transform;
  (*state)->mark_transform_changed(entity_);
  return error::success;
}
error game_object::set_name(std::string_view name) const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  auto* object = (*state)->get<detail::object_state>(entity_.get());
  if (!object)
    return error::invalid_handle;
  object->name = std::string(name);
  return error::success;
}
result<game_object> game_object::parent() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* object = (*state)->get<detail::object_state>(entity_.get());
  if (!object)
    return error::invalid_handle;
  return object->parent ? owner_->object(object->parent) : result<game_object>{game_object{}};
}
error game_object::reparent(game_object parent) const {
  if (!owner_ || (parent.owner_ && parent.owner_ != owner_))
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  auto* object = (*state)->get<detail::object_state>(entity_.get());
  auto* target = (*state)->get<detail::object_state>(parent.entity_.get());
  if (!object || (parent.entity_ && !target))
    return error::invalid_handle;
  if (target && object->scene != target->scene)
    return error::invalid_argument;
  for (auto ancestor = parent.entity_; ancestor;) {
    if (ancestor == entity_)
      return error::invalid_argument;
    ancestor = (*state)->get<detail::object_state>(ancestor.get())->parent;
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
