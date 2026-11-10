// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/scene/game_object.hpp"
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/detail/world_state.hpp"
#include "engine/function/scene/world.hpp"
#include <cassert>
#include <string>
#include <utility>

namespace gneiss::engine {
result<std::vector<game_object>> game_object::children() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* ids = (*state)->scene().children(entity_);
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
  return *(*state)->scene().name(entity_);
}
result<math::transform> game_object::local_transform() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* value = (*state)->scene().get_local(entity_id{entity_.get()});
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
  const auto& hierarchy = (*state)->scene();
  auto node = entity_id{entity_.get()};
  const auto* local = hierarchy.get_local(node);
  if (!local)
    return error::invalid_handle;
  auto matrix = math::model_matrix(*local);
  while ((node = hierarchy.get_parent(node))) {
    local = hierarchy.get_local(node);
    if (!local)
      return error::invalid_handle;
    matrix = math::model_matrix(*local) * matrix;
  }
  return matrix;
}
result<math::transform> game_object::world_transform() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return (*state)->scene().get_world(entity_id{entity_.get()});
}
error game_object::set_local_transform(const math::transform& transform) const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return (*state)->scene().set_local(entity_id{entity_.get()}, transform);
}
error game_object::set_name(std::string_view name) const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return (*state)->scene().set_name(entity_, std::string(name));
}
result<game_object> game_object::parent() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto& hierarchy = (*state)->scene();
  if (!hierarchy.get_local(entity_id{entity_.get()}))
    return error::invalid_handle;
  auto parent = hierarchy.get_parent(entity_id{entity_.get()});
  return parent ? owner_->object(entity_id{parent.get()}) : result<game_object>{game_object{}};
}
error game_object::reparent(game_object parent) const {
  if (!owner_ || (parent.owner_ && parent.owner_ != owner_))
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto node = entity_id{entity_.get()};
  const auto parent_node = entity_id{parent.entity_.get()};
  if (!(*state)->scene().get_local(node) ||
      (parent_node && !(*state)->scene().get_local(parent_node)))
    return error::invalid_handle;
  return (*state)->scene().reparent(node, parent_node);
}
error game_object::destroy() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  return state ? (*state)->scene().destroy(entity_id{entity_.get()}) : state.error();
}
}
