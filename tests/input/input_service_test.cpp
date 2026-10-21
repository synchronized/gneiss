// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/input/input_service.hpp"

using namespace gneiss::engine::input;
using namespace gneiss::engine::core::input;
#include <limits>

int main() {
  gneiss::engine::input::input_service input;
  input_event key = {};
  key.type = event_type::key;
  key.window_id = 1;
  key.data.key.physical_key = 26U;
  key.data.key.modifiers = 1U;
  key.data.key.action = key_action::pressed;
  if (!input.push(key) ||
      (input.keyboard().pressed_keys[26U / 64U] & (UINT64_C(1) << (26U % 64U))) == 0U ||
      input.keyboard().modifiers != 1U) {
    return 1;
  }

  input_event pointer = {};
  pointer.type = event_type::pointer_button;
  pointer.data.pointer_button.x = 10.0F;
  pointer.data.pointer_button.y = 20.0F;
  pointer.data.pointer_button.buttons = 1U;
  if (!input.push(pointer) || input.pointer().x != 10.0F || input.pointer().y != 20.0F ||
      input.pointer().buttons != 1U) {
    return 2;
  }

  input_event output = {};
  if (input.poll(output) != gneiss::result::success || output.type != event_type::key ||
      input.poll(output) != gneiss::result::success || output.type != event_type::pointer_button ||
      input.poll(output) != gneiss::result::not_ready) {
    return 3;
  }

  input.clear_focus();
  if (input.keyboard().pressed_keys[26U / 64U] != 0U || input.pointer().buttons != 0U) {
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

  gneiss::engine::resource::action_map map;
  map.actions.push_back({.name = "move", .bindings = {{26U, 1.0F}}});
  input.clear_focus();
  if (input.replace_action_map(std::move(map)) != gneiss::result::success) {
    return 7;
  }
  gneiss::engine::core::input::action_id action;
  gneiss::engine::core::input::action_state state;
  input.begin_frame();
  key.data.key.action = key_action::pressed;
  if (input.find_action("move", action) != gneiss::result::success || !input.push(key) ||
      input.get_action_state(action, state) != gneiss::result::success || state.pressed == 0U ||
      state.held == 0U || state.value != 1.0F) {
    return 7;
  }
  key.data.key.action = key_action::released;
  if (!input.push(key) || input.get_action_state(action, state) != gneiss::result::success ||
      state.pressed == 0U || state.released == 0U || state.held != 0U) {
    return 8;
  }
  using gneiss::result;
  using namespace gneiss::engine::input;
  using namespace gneiss::engine::core::input;
  const auto original = action;
  gneiss::engine::resource::action_map candidate{{{"move", {{26U, 1.0F}}}}};
  auto invalid = candidate;
  invalid.actions[0].bindings[0].physical_key = 256U;
  if (input.replace_action_map(invalid) != result::invalid_argument ||
      input.get_action_state(original, state) != result::success) {
    return 9;
  }
  invalid = candidate;
  invalid.actions[0].bindings[0].scale = std::numeric_limits<float>::quiet_NaN();
  if (input.replace_action_map(invalid) != result::invalid_argument) {
    return 10;
  }
  invalid = candidate;
  invalid.actions.push_back(invalid.actions.front());
  if (input.replace_action_map(invalid) != result::invalid_argument ||
      input.replace_action_map({}) != result::invalid_argument ||
      input.find_action("move", action) != result::success || action != original) {
    return 11;
  }
  input_service other;
  if (other.replace_action_map(candidate) != result::success ||
      other.get_action_state(original, state) != result::invalid_handle ||
      input.get_action_state({}, state) != result::invalid_handle ||
      input.replace_action_map(candidate) != result::success ||
      input.get_action_state(original, state) != result::invalid_handle || state.pressed ||
      state.held || state.released || state.value != 0.0F) {
    return 12;
  }
  input.begin_frame();
  if (input.find_action("move", action) != result::success) {
    return 13;
  }
  key.data.key.action = key_action::pressed;
  if (!input.push(key)) {
    return 14;
  }
  input.begin_frame();
  if (input.get_action_state(action, state) != result::success || state.pressed || !state.held) {
    return 15;
  }
  input.clear_focus();
  if (input.get_action_state(action, state) != result::success || state.held || !state.released) {
    return 16;
  }
  input.begin_frame();
  input_event invalid_event{};
  invalid_event.type = event_type::text;
  invalid_event.data.text = {};
  invalid_event.data.text.length = 49;
  if (input.push(invalid_event))
    return 17;
  invalid_event.type = static_cast<event_type>(99);
  if (input.push(invalid_event))
    return 18;
  invalid_event.type = event_type::key;
  invalid_event.data.key = {26, 0, 0, static_cast<key_action>(99)};
  if (input.push(invalid_event) || input.poll(output) != result::not_ready)
    return 19;
  input_event enter{};
  enter.type = event_type::pointer_entered;
  if (!input.push(enter) || !input.pointer().is_inside)
    return 20;
  enter.type = event_type::pointer_left;
  if (!input.push(enter) || input.pointer().is_inside)
    return 21;
  input.begin_frame();
  if (input.poll(output) != result::not_ready || output.type != event_type::none)
    return 22;
  return 0;
}
