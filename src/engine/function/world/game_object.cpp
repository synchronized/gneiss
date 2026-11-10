// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/world/game_object.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_component_access.hpp"
#include "engine/function/world/world_state.hpp"
#include <string>
#include <utility>

namespace gneiss::engine {
bool game_object::is_valid() const noexcept {
  return owner_ && owner_->entity_node(entity_).has_value();
}
result<scene_node_identity_view> game_object::identity() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return (*state)->scene().identity(scene_node_id{entity_.get()});
}
error game_object::set_identity(scene_node_identity identity) const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return (*state)->scene().set_identity(scene_node_id{entity_.get()}, std::move(identity));
}
result<math::transform> game_object::local_transform() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto* value = (*state)->scene().get_local(scene_node_id{entity_.get()});
  return value ? result<math::transform>{*value} : result<math::transform>{error::invalid_handle};
}
result<scene_instance> game_object::scene() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return scene_instance::borrow(*owner_, (*state)->scene_of(scene_node_id{entity_.get()}));
}
result<math::matrix4> game_object::world_matrix() const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  const auto& hierarchy = (*state)->scene();
  auto node = scene_node_id{entity_.get()};
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
  return (*state)->scene().get_world(scene_node_id{entity_.get()});
}
error game_object::set_local_transform(const math::transform& transform) const {
  if (!owner_)
    return error::invalid_handle;
  auto state = detail::world_component_access::get(*owner_);
  if (!state)
    return state.error();
  return (*state)->scene().set_local(scene_node_id{entity_.get()}, transform);
}
error game_object::set_name(std::string_view name) const {
  auto current = identity();
  if (!current)
    return current.error();
  return set_identity({current->uuid, std::string(name)});
}
result<game_object> game_object::parent() const {
  if (!owner_)
    return error::invalid_handle;
  scene_node_id parent;
  auto status = owner_->node_get_parent(scene_node_id{entity_.get()}, parent);
  if (status.failed())
    return status;
  return parent ? owner_->object(entity_id{parent.get()}) : result<game_object>{game_object{}};
}
error game_object::reparent(game_object parent) const {
  if (!owner_ || (parent.owner_ && parent.owner_ != owner_))
    return error::invalid_handle;
  return owner_->node_reparent(scene_node_id{entity_.get()}, scene_node_id{parent.entity_.get()});
}
error game_object::destroy() const {
  return owner_ ? owner_->object_destroy(scene_node_id{entity_.get()}) : error::invalid_handle;
}
}
