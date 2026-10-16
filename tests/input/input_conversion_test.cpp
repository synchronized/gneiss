// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/input_conversion.hpp"
#include <algorithm>

using namespace gneiss::input_internal;
using gneiss::abi_internal::input_event_to_abi;
using gneiss::abi_internal::input_state_to_abi;

int main() {
  input_event event{};
  event.type = event_type::key;
  event.window_id = 42;
  event.timestamp_ns = 100;
  event.data.key = {26, 0, 3, key_action::repeated};
  auto output = input_event_to_abi(event);
  if (output.struct_size != sizeof(output) || output.type != GNEISS_INPUT_EVENT_KEY ||
      output.window_id != 42 || output.timestamp_ns != 100 || output.data.key.physical_key != 26 ||
      output.data.key.modifiers != 3 || output.data.key.action != GNEISS_KEY_REPEATED)
    return 1;
  event.type = event_type::text;
  event.data.text = {};
  event.data.text.length = 48;
  std::fill(std::begin(event.data.text.utf8), std::end(event.data.text.utf8), 'x');
  output = input_event_to_abi(event);
  if (output.type != GNEISS_INPUT_EVENT_TEXT || output.data.text.length != 48 ||
      output.data.text.utf8[47] != 'x' ||
      !std::all_of(std::begin(output.data.text.reserved), std::end(output.data.text.reserved),
                   [](auto byte) { return byte == 0; }))
    return 2;
  event.data.text.length = 3;
  output = input_event_to_abi(event);
  if (output.data.text.utf8[2] != 'x' || output.data.text.utf8[3] != 0 ||
      output.data.text.utf8[47] != 0)
    return 9;
  event.type = event_type::pointer_moved;
  event.data.pointer_moved = {1, 2, 3, 4, 5};
  output = input_event_to_abi(event);
  if (output.type != GNEISS_INPUT_EVENT_POINTER_MOVED || output.data.pointer_moved.x != 1 ||
      output.data.pointer_moved.y != 2 || output.data.pointer_moved.delta_x != 3 ||
      output.data.pointer_moved.delta_y != 4 || output.data.pointer_moved.buttons != 5)
    return 3;
  event.type = event_type::pointer_wheel;
  event.data.pointer_wheel = {5, 4, -3, -2, 1};
  output = input_event_to_abi(event);
  if (output.type != GNEISS_INPUT_EVENT_POINTER_WHEEL || output.data.pointer_wheel.x != 5 ||
      output.data.pointer_wheel.y != 4 || output.data.pointer_wheel.delta_x != -3 ||
      output.data.pointer_wheel.delta_y != -2 || output.data.pointer_wheel.buttons != 1)
    return 4;
  event.type = event_type::pointer_button;
  event.data.pointer_button = {3, 4, 2, true, 2};
  output = input_event_to_abi(event);
  if (output.type != GNEISS_INPUT_EVENT_POINTER_BUTTON || output.data.pointer_button.x != 3 ||
      output.data.pointer_button.y != 4 || output.data.pointer_button.button != 2 ||
      output.data.pointer_button.pressed != 1 || output.data.pointer_button.buttons != 2)
    return 5;
  for (auto type : {event_type::none, event_type::pointer_entered, event_type::pointer_left}) {
    event.type = type;
    output = input_event_to_abi(event);
    if (output.type != static_cast<std::uint32_t>(type) ||
        !std::all_of(std::begin(output.data.reserved), std::end(output.data.reserved),
                     [](auto byte) { return byte == 0; }))
      return 6;
  }
  keyboard_state keyboard{};
  keyboard.modifiers = 5;
  keyboard.pressed_keys = {1, 2, 4, 8};
  const auto keys = input_state_to_abi(keyboard);
  if (keys.struct_size != sizeof(keys) || keys.modifiers != 5 || keys.pressed_keys[3] != 8 ||
      keys.reserved[0] != 0)
    return 7;
  const auto pointer = input_state_to_abi(pointer_state{3, 1.5F, -2.5F, true});
  if (pointer.struct_size != sizeof(pointer) || pointer.buttons != 3 || pointer.x != 1.5F ||
      pointer.y != -2.5F || pointer.is_inside != 1 || pointer.reserved[0] != 0)
    return 8;
  return 0;
}
