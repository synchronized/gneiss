// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/scene/detail/world_component_access.hpp"
#include "engine/function/scene/game_object.hpp"
#include <cassert>
#include <type_traits>
#include <utility>
namespace gneiss::engine::detail {
// 仅在组件实现单元实例化，不把 EnTT 传播到对象使用方。
template <class T> struct component_policy {
  static error validate(const T&) { return error::success; }
  static void removed(world_state&, entity_id) {}
};
inline result<world_state*> component_state(game_object object) {
  if (!object.owner())
    return error::invalid_handle;
  auto state = world_component_access::get(*object.owner());
  if (!state)
    return state.error();
  if (!(*state)->is_alive(object.entity().get()))
    return error::invalid_handle;
  return *state;
}
}
namespace gneiss::engine {
template <class T> T* game_object::try_component() const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "受控对象数据不得通过普通组件接口访问");
  auto state = detail::component_state(*this);
  assert(state && is_valid());
  return state ? (*state)->template get<T>(entity_.get()) : nullptr;
}
template <class T> error game_object::insert_component(T value) const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "受控对象数据不得通过普通组件接口修改");
  auto state = detail::component_state(*this);
  if (!state)
    return state.error();
  if ((*state)->template get<T>(entity_.get()))
    return error::invalid_state;
  auto status = detail::component_policy<T>::validate(value);
  if (status.failed())
    return status;
  (*state)->template emplace<T>(entity_.get(), std::move(value));
  return error::success;
}
template <class T> result<T> game_object::read_component() const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "场景归属必须通过场景接口访问");
  auto state = detail::component_state(*this);
  if (!state)
    return state.error();
  const auto* component = (*state)->template get<T>(entity_.get());
  if (!component)
    return error::not_found;
  return *component;
}
template <class T> error game_object::set_component(T value) const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "场景归属必须通过场景接口修改");
  auto state = detail::component_state(*this);
  if (!state)
    return state.error();
  auto validated = detail::component_policy<T>::validate(value);
  if (validated.failed())
    return validated;
  (*state)->template emplace_or_replace<T>(entity_.get(), std::move(value));
  return error::success;
}
template <class T> error game_object::remove_component() const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "场景归属必须通过场景接口修改");
  auto state = detail::component_state(*this);
  if (!state)
    return state.error();
  auto status = (*state)->template remove<T>(entity_.get());
  if (status.ok())
    detail::component_policy<T>::removed(**state, entity_);
  return status;
}
}
