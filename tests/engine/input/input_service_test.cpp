// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/input/service.hpp>

using namespace gneiss;
using namespace gneiss;
#include <limits>

int main() {
  gneiss::input::service input;
  gneiss::input::event key = {};
  key.type = gneiss::input::event_type::key;
  key.window_id = {1};
  key.data.key.physical_key = 26U;
  key.data.key.modifiers = 1U;
  key.data.key.action = gneiss::input::key_action::pressed;
  if (!input.push(key) ||
      (input.keyboard().pressed_keys[26U / 64U] & (UINT64_C(1) << (26U % 64U))) == 0U ||
      input.keyboard().modifiers != 1U) {
    return 1;
  }

  gneiss::input::event pointer = {};
  pointer.type = gneiss::input::event_type::pointer_button;
  pointer.data.pointer_button.x = 10.0F;
  pointer.data.pointer_button.y = 20.0F;
  pointer.data.pointer_button.buttons = 1U;
  if (!input.push(pointer) || input.pointer().x != 10.0F || input.pointer().y != 20.0F ||
      input.pointer().buttons != 1U) {
    return 2;
  }

  gneiss::input::event output = {};
  if (input.poll(output) != gneiss::error::success ||
      output.type != gneiss::input::event_type::key ||
      input.poll(output) != gneiss::error::success ||
      output.type != gneiss::input::event_type::pointer_button ||
      input.poll(output) != gneiss::error::not_ready) {
    return 3;
  }

  input.clear_focus();
  if (input.keyboard().pressed_keys[26U / 64U] != 0U || input.pointer().buttons != 0U) {
    return 4;
  }

  input.begin_frame();
  for (std::size_t index = 0; index < 1024; ++index) {
    if (!input.push(key)) {
      return 5;
    }
  }
  for (std::size_t index = 0; index < 1024; ++index) {
    if (input.poll(output) != error::success || output.window_id != key.window_id)
      return 6;
  }
  if (input.poll(output) != error::not_ready)
    return 6;

  gneiss::input::action_map_description map;
  map.actions.push_back({.name = "move", .bindings = {{26U, 1.0F}}});
  input.clear_focus();
  if (input.replace_action_map(std::move(map)) != gneiss::error::success) {
    return 7;
  }
  gneiss::input::action_id action;
  gneiss::input::action_state state;
  input.begin_frame();
  key.data.key.action = gneiss::input::key_action::pressed;
  if (input.find_action("move", action) != gneiss::error::success || !input.push(key) ||
      input.get_action_state(action, state) != gneiss::error::success || state.pressed == 0U ||
      state.held == 0U || state.value != 1.0F) {
    return 7;
  }
  key.data.key.action = gneiss::input::key_action::released;
  if (!input.push(key) || input.get_action_state(action, state) != gneiss::error::success ||
      state.pressed == 0U || state.released == 0U || state.held != 0U) {
    return 8;
  }

  using namespace gneiss;
  using namespace gneiss;
  const auto original = action;
  gneiss::input::action_map_description candidate{{{"move", {{26U, 1.0F}}}}};
  auto invalid = candidate;
  invalid.actions[0].bindings[0].physical_key = 256U;
  if (input.replace_action_map(invalid) != gneiss::error::invalid_argument ||
      input.get_action_state(original, state) != gneiss::error::success) {
    return 9;
  }
  invalid = candidate;
  invalid.actions[0].bindings[0].scale = std::numeric_limits<float>::quiet_NaN();
  if (input.replace_action_map(invalid) != gneiss::error::invalid_argument) {
    return 10;
  }
  invalid = candidate;
  invalid.actions.push_back(invalid.actions.front());
  if (input.replace_action_map(invalid) != gneiss::error::invalid_argument ||
      input.replace_action_map({}) != gneiss::error::invalid_argument ||
      input.find_action("move", action) != gneiss::error::success || action != original) {
    return 11;
  }
  gneiss::input::service other;
  if (other.replace_action_map(candidate) != gneiss::error::success ||
      other.get_action_state(original, state) != gneiss::error::invalid_handle ||
      input.get_action_state({}, state) != gneiss::error::invalid_handle ||
      input.replace_action_map(candidate) != gneiss::error::success ||
      input.get_action_state(original, state) != gneiss::error::invalid_handle || state.pressed ||
      state.held || state.released || state.value != 0.0F) {
    return 12;
  }
  input.begin_frame();
  if (input.find_action("move", action) != gneiss::error::success) {
    return 13;
  }
  key.data.key.action = gneiss::input::key_action::pressed;
  if (!input.push(key)) {
    return 14;
  }
  input.begin_frame();
  if (input.get_action_state(action, state) != gneiss::error::success || state.pressed ||
      !state.held) {
    return 15;
  }
  input.clear_focus();
  if (input.get_action_state(action, state) != gneiss::error::success || state.held ||
      !state.released) {
    return 16;
  }
  input.begin_frame();
  gneiss::input::event invalid_event{};
  invalid_event.type = gneiss::input::event_type::text;
  invalid_event.data.text = {};
  invalid_event.data.text.length = 49;
  if (input.push(invalid_event))
    return 17;
  invalid_event.type = static_cast<gneiss::input::event_type>(99);
  if (input.push(invalid_event))
    return 18;
  invalid_event.type = gneiss::input::event_type::key;
  invalid_event.data.key = {26, 0, 0, static_cast<gneiss::input::key_action>(99)};
  if (input.push(invalid_event) || input.poll(output) != gneiss::error::not_ready)
    return 19;
  gneiss::input::event enter{};
  enter.type = gneiss::input::event_type::pointer_entered;
  if (!input.push(enter) || !input.pointer().is_inside)
    return 20;
  enter.type = gneiss::input::event_type::pointer_left;
  if (!input.push(enter) || input.pointer().is_inside)
    return 21;
  input.begin_frame();
  if (input.poll(output) != gneiss::error::not_ready ||
      output.type != gneiss::input::event_type::none)
    return 22;
  return 0;
}
