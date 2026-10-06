// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_GAME_CONTEXT_INL_
#define GNEISS_DETAIL_GAME_CONTEXT_INL_

#include <gneiss/engine/game_module.hpp>

namespace gneiss {

inline result game_context::get_world_native(gneiss_world& out_world) const noexcept {
  return from_native(gneiss_game_context_get_world(value_, &out_world));
}

inline result game_context::get_world(world_ref& output) const noexcept {
  gneiss_world value = GNEISS_NULL_WORLD;
  const auto status = get_world_native(value);
  if (status.ok()) {
    output = world_ref{value};
  }
  return status;
}

inline result
game_context::get_startup_root_entity_native(gneiss_entity_id& out_entity) const noexcept {
  return from_native(gneiss_game_context_get_startup_root_entity(value_, &out_entity));
}

inline result game_context::get_startup_root_entity(entity_id& output) const noexcept {
  gneiss_entity_id value = GNEISS_NULL_ENTITY_ID;
  const auto status = get_startup_root_entity_native(value);
  if (status.ok()) {
    output = entity_id{value};
  }
  return status;
}

inline result game_context::find_action_native(std::string_view name,
                                               gneiss_action& out_action) const noexcept {
  return from_native(
      gneiss_game_context_find_action(value_, name.data(), name.size(), &out_action));
}

inline result game_context::get_action_state_native(gneiss_action action,
                                                    gneiss_action_state& out_state) const noexcept {
  return from_native(gneiss_game_context_get_action_state(value_, action, &out_state));
}

inline result game_context::find_action(std::string_view name, action_id& output) const noexcept {
  gneiss_action value = GNEISS_NULL_ACTION;
  const auto status = find_action_native(name, value);
  if (status.ok()) {
    output = action_id{value};
  }
  return status;
}

inline result game_context::get_action_state(action_id id, action_state& output) const noexcept {
  gneiss_action_state native = GNEISS_ACTION_STATE_INIT;
  const auto status = get_action_state_native(id.get(), native);
  if (status.ok()) {
    output = detail::from_action_state(native);
  }
  return status;
}

inline result game_context::request_exit() const noexcept {
  return from_native(gneiss_game_context_request_exit(value_));
}

inline result game_context::log(const log_message& message) const noexcept {
  const auto native = to_native(message);
  return from_native(gneiss_game_context_log(value_, &native));
}

} // namespace gneiss

#endif
