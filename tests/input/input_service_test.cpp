// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "input/input_service.h"

#include <gneiss/input.h>

int main() {
  gneiss::input_internal::input_service input;
  gneiss_input_event key = GNEISS_INPUT_EVENT_INIT;
  key.type = GNEISS_INPUT_EVENT_KEY;
  key.window_id = 1;
  key.data.key.physical_key = GNEISS_PHYSICAL_KEY_W;
  key.data.key.modifiers = GNEISS_MODIFIER_LEFT_SHIFT_BIT;
  key.data.key.action = GNEISS_KEY_PRESSED;
  if (!input.push(key) ||
      (input.keyboard().pressed_keys[GNEISS_PHYSICAL_KEY_W / 64U] &
       (UINT64_C(1) << (GNEISS_PHYSICAL_KEY_W % 64U))) == 0U ||
      input.keyboard().modifiers != GNEISS_MODIFIER_LEFT_SHIFT_BIT) {
    return 1;
  }

  gneiss_input_event pointer = GNEISS_INPUT_EVENT_INIT;
  pointer.type = GNEISS_INPUT_EVENT_POINTER_BUTTON;
  pointer.data.pointer_button.x = 10.0F;
  pointer.data.pointer_button.y = 20.0F;
  pointer.data.pointer_button.buttons = GNEISS_POINTER_PRIMARY_BIT;
  if (!input.push(pointer) || input.pointer().x != 10.0F || input.pointer().y != 20.0F ||
      input.pointer().buttons != GNEISS_POINTER_PRIMARY_BIT) {
    return 2;
  }

  gneiss_input_event output = GNEISS_INPUT_EVENT_INIT;
  if (input.poll(output) != GNEISS_SUCCESS || output.type != GNEISS_INPUT_EVENT_KEY ||
      input.poll(output) != GNEISS_SUCCESS || output.type != GNEISS_INPUT_EVENT_POINTER_BUTTON ||
      input.poll(output) != GNEISS_ERROR_NOT_READY) {
    return 3;
  }

  input.clear_focus();
  if (input.keyboard().pressed_keys[GNEISS_PHYSICAL_KEY_W / 64U] != 0U ||
      input.pointer().buttons != 0U) {
    return 4;
  }

  input.begin_frame();
  for (std::size_t index = 0; index < input.event_capacity; ++index) {
    if (!input.push(key)) {
      return 5;
    }
  }
  if (input.push(key)) {
    return 6;
  }

  gneiss::input_internal::action_map map;
  map.actions.push_back({.name = "move", .bindings = {{GNEISS_PHYSICAL_KEY_W, 1.0F}}});
  input.clear_focus();
  if (input.replace_action_map(std::move(map)) != GNEISS_SUCCESS) {
    return 7;
  }
  gneiss_action action = GNEISS_NULL_ACTION;
  gneiss_action_state state = GNEISS_ACTION_STATE_INIT;
  input.begin_frame();
  key.data.key.action = GNEISS_KEY_PRESSED;
  if (input.find_action("move", action) != GNEISS_SUCCESS || !input.push(key) ||
      input.get_action_state(action, state) != GNEISS_SUCCESS || state.pressed == 0U ||
      state.held == 0U || state.value != 1.0F) {
    return 7;
  }
  key.data.key.action = GNEISS_KEY_RELEASED;
  if (!input.push(key) || input.get_action_state(action, state) != GNEISS_SUCCESS ||
      state.pressed == 0U || state.released == 0U || state.held != 0U) {
    return 8;
  }
  return 0;
}
