// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_INPUT_INL_
#define GNEISS_DETAIL_INPUT_INL_

#include <gneiss/engine/input.hpp>

namespace gneiss {

namespace detail {
[[nodiscard]] inline action_state from_action_state(const gneiss_action_state& value) noexcept {
  return {
      .pressed = value.pressed != 0,
      .held = value.held != 0,
      .released = value.released != 0,
      .value = value.value,
  };
}
// 所有分支先转换到候选；未知事件、非法文本长度或动作失败时保留输出。
[[nodiscard]] inline result from_input_event(const gneiss_input_event& value,
                                             input_event& output) noexcept {
  input_event candidate;
  candidate.window_id = value.window_id;
  candidate.timestamp_ns = value.timestamp_ns;
  switch (value.type) {
  case GNEISS_INPUT_EVENT_KEY: {
    const auto& key = value.data.key;
    if (key.action > GNEISS_KEY_REPEATED) {
      return result::unsupported;
    }
    candidate.data = key_event{
        .physical = static_cast<physical_key>(key.physical_key),
        .logical = static_cast<logical_key>(key.logical_key),
        .modifiers = static_cast<input_modifier>(key.modifiers),
        .action = static_cast<key_action>(key.action),
    };
    break;
  }
  case GNEISS_INPUT_EVENT_TEXT: {
    if (value.data.text.length > input_text_capacity) {
      return result::invalid_argument;
    }
    text_event text;
    text.length = value.data.text.length;
    std::copy_n(value.data.text.utf8, text.length, text.utf8.begin());
    candidate.data = text;
    break;
  }
  case GNEISS_INPUT_EVENT_POINTER_MOVED: {
    const auto& pointer = value.data.pointer_moved;
    candidate.data = pointer_moved_event{
        .x = pointer.x,
        .y = pointer.y,
        .delta_x = pointer.delta_x,
        .delta_y = pointer.delta_y,
        .buttons = static_cast<pointer_button>(pointer.buttons),
    };
    break;
  }
  case GNEISS_INPUT_EVENT_POINTER_BUTTON: {
    const auto& pointer = value.data.pointer_button;
    candidate.data = pointer_button_event{
        .x = pointer.x,
        .y = pointer.y,
        .button = static_cast<pointer_button>(pointer.button),
        .pressed = pointer.pressed != 0,
        .buttons = static_cast<pointer_button>(pointer.buttons),
    };
    break;
  }
  case GNEISS_INPUT_EVENT_POINTER_WHEEL: {
    const auto& pointer = value.data.pointer_wheel;
    candidate.data = pointer_wheel_event{
        .x = pointer.x,
        .y = pointer.y,
        .delta_x = pointer.delta_x,
        .delta_y = pointer.delta_y,
        .buttons = static_cast<pointer_button>(pointer.buttons),
    };
    break;
  }
  case GNEISS_INPUT_EVENT_POINTER_ENTERED:
    candidate.data = pointer_entered_event{};
    break;
  case GNEISS_INPUT_EVENT_POINTER_LEFT:
    candidate.data = pointer_left_event{};
    break;
  default:
    return result::unsupported;
  }
  output = candidate;
  return result::success;
}
} // namespace detail

[[nodiscard]] inline result poll_input(gneiss_application application,
                                       input_event& out_event) noexcept {
  gneiss_input_event native = GNEISS_INPUT_EVENT_INIT;
  const auto status = from_native(gneiss_application_poll_input(application, &native));
  return status.ok() ? detail::from_input_event(native, out_event) : status;
}

[[nodiscard]] inline result get_keyboard_state(gneiss_application application,
                                               keyboard_state& out_state) noexcept {
  gneiss_keyboard_state native = GNEISS_KEYBOARD_STATE_INIT;
  const auto status = from_native(gneiss_application_get_keyboard_state(application, &native));
  if (status.ok()) {
    out_state.modifiers = static_cast<input_modifier>(native.modifiers);
    std::copy_n(native.pressed_keys, out_state.pressed_keys.size(), out_state.pressed_keys.begin());
  }
  return status;
}

[[nodiscard]] inline result get_pointer_state(gneiss_application application,
                                              pointer_state& out_state) noexcept {
  gneiss_pointer_state native = GNEISS_POINTER_STATE_INIT;
  const auto status = from_native(gneiss_application_get_pointer_state(application, &native));
  if (status.ok()) {
    out_state = {
        .buttons = static_cast<pointer_button>(native.buttons),
        .x = native.x,
        .y = native.y,
        .is_inside = native.is_inside != 0,
    };
  }
  return status;
}

[[nodiscard]] inline result load_action_map(gneiss_application application,
                                            std::string_view uri) noexcept {
  return from_native(gneiss_application_load_action_map(application, uri.data(), uri.size()));
}

[[nodiscard]] inline result find_action(gneiss_application application, std::string_view name,
                                        action_id& out_action) noexcept {
  gneiss_action value = GNEISS_NULL_ACTION;
  const auto status =
      from_native(gneiss_application_find_action(application, name.data(), name.size(), &value));
  if (status.ok()) {
    out_action = action_id{value};
  }
  return status;
}

[[nodiscard]] inline result get_action_state(gneiss_application application, action_id value,
                                             action_state& out_state) noexcept {
  gneiss_action_state native = GNEISS_ACTION_STATE_INIT;
  const auto status =
      from_native(gneiss_application_get_action_state(application, value.get(), &native));
  if (status.ok()) {
    out_state = detail::from_action_state(native);
  }
  return status;
}

} // namespace gneiss

#endif
