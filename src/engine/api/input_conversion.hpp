// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/input/input_types.hpp"
#include <algorithm>
#include <gneiss/engine/input.h>
#include <iterator>

namespace gneiss::engine::detail {
/** 仅转换有效原生事件；保留字段含义并将 ABI 保留位清零。 */
inline gneiss_input_event input_event_to_abi(const gneiss::engine::input_event& source) noexcept {
  gneiss_input_event output = GNEISS_INPUT_EVENT_INIT;
  output.type = static_cast<std::uint32_t>(source.type);
  output.window_id = source.window_id;
  output.timestamp_ns = source.timestamp_ns;
  switch (source.type) {
  case gneiss::engine::event_type::key:
    output.data.key.physical_key = source.data.key.physical_key;
    output.data.key.logical_key = source.data.key.logical_key;
    output.data.key.modifiers = source.data.key.modifiers;
    output.data.key.action = static_cast<std::uint32_t>(source.data.key.action);
    break;
  case gneiss::engine::event_type::text:
    output.data.text.length = source.data.text.length;
    std::copy_n(source.data.text.utf8, source.data.text.length, output.data.text.utf8);
    break;
  case gneiss::engine::event_type::pointer_moved:
    output.data.pointer_moved.x = source.data.pointer_moved.x;
    output.data.pointer_moved.y = source.data.pointer_moved.y;
    output.data.pointer_moved.delta_x = source.data.pointer_moved.delta_x;
    output.data.pointer_moved.delta_y = source.data.pointer_moved.delta_y;
    output.data.pointer_moved.buttons = source.data.pointer_moved.buttons;
    break;
  case gneiss::engine::event_type::pointer_button:
    output.data.pointer_button.x = source.data.pointer_button.x;
    output.data.pointer_button.y = source.data.pointer_button.y;
    output.data.pointer_button.button = source.data.pointer_button.button;
    output.data.pointer_button.pressed =
        static_cast<std::uint32_t>(source.data.pointer_button.pressed);
    output.data.pointer_button.buttons = source.data.pointer_button.buttons;
    break;
  case gneiss::engine::event_type::pointer_wheel:
    output.data.pointer_wheel.x = source.data.pointer_wheel.x;
    output.data.pointer_wheel.y = source.data.pointer_wheel.y;
    output.data.pointer_wheel.delta_x = source.data.pointer_wheel.delta_x;
    output.data.pointer_wheel.delta_y = source.data.pointer_wheel.delta_y;
    output.data.pointer_wheel.buttons = source.data.pointer_wheel.buttons;
    break;
  default:
    break;
  }
  return output;
}
inline gneiss_keyboard_state
input_state_to_abi(const gneiss::engine::keyboard_state& source) noexcept {
  gneiss_keyboard_state output = GNEISS_KEYBOARD_STATE_INIT;
  output.modifiers = source.modifiers;
  std::copy(source.pressed_keys.begin(), source.pressed_keys.end(), output.pressed_keys);
  return output;
}
inline gneiss_pointer_state
input_state_to_abi(const gneiss::engine::pointer_state& source) noexcept {
  gneiss_pointer_state output = GNEISS_POINTER_STATE_INIT;
  output.buttons = source.buttons;
  output.x = source.x;
  output.y = source.y;
  output.is_inside = source.is_inside ? 1U : 0U;
  return output;
}
} // namespace gneiss::engine::detail
