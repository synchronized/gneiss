// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "function/scene/world_state.hpp"
#include <gneiss/engine/core/base/assert.hpp>
#include <gneiss/engine/function/scene/game_object.hpp>
#include <type_traits>
#include <utility>
namespace gneiss::detail {
// 仅在组件实现单元实例化，不把 EnTT 传播到对象使用方。
template <class T> struct component_policy {
  static void removed(world_state&, entity_id) {}
};
inline world_state& component_state(game_object object) {
  GNEISS_ASSERT(object.is_valid(), "组件操作要求有效 GameObject 及所属线程");
  return state_of(*object.owner());
}
}
namespace gneiss {
template <class T> T* game_object::try_component() const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "受控对象数据不得通过普通组件接口访问");
  auto& state = detail::component_state(*this);
  return state.template get<T>(entity_.get());
}
template <class T> error game_object::insert_component(T value) const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "受控对象数据不得通过普通组件接口修改");
  auto& state = detail::component_state(*this);
  if (state.template get<T>(entity_.get()))
    return error::invalid_state;
  state.template emplace<T>(entity_.get(), std::move(value));
  return error::success;
}
template <class T> error game_object::remove_component() const {
  static_assert(!std::is_same_v<T, detail::object_state> &&
                    !std::is_same_v<T, detail::auxiliary_scene_membership>,
                "场景归属必须通过场景接口修改");
  auto& state = detail::component_state(*this);
  auto status = state.template remove<T>(entity_.get());
  if (status.ok())
    detail::component_policy<T>::removed(state, entity_);
  return status;
}
}

namespace gneiss {
template <class T> error game_object::notify_component_modified() const {
  auto& state = detail::component_state(*this);
  if (!state.template get<T>(entity_.get()))
    return error::not_found;
  state.changes.component(entity_, runtime_type_of<T>());
  return error::success;
}
}
