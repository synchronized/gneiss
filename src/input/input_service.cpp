// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "input/input_service.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {
std::atomic_uint32_t next_action_generation{1};
}

namespace gneiss::input_internal {

void input_service::begin_frame() noexcept {
  read_index_ = 0;
  event_count_ = 0;
  for (auto& state : action_states_) {
    state.pressed = 0;
    state.released = 0;
  }
}

bool input_service::push(const gneiss_input_event& event) noexcept {
  if (event_count_ == event_capacity) {
    return false;
  }
  events_[event_count_++] = event;
  apply(event);
  update_actions();
  return true;
}

gneiss_result input_service::poll(gneiss_input_event& out_event) noexcept {
  out_event = GNEISS_INPUT_EVENT_INIT;
  if (read_index_ == event_count_) {
    return GNEISS_ERROR_NOT_READY;
  }
  out_event = events_[read_index_++];
  return GNEISS_SUCCESS;
}

void input_service::clear_focus() noexcept {
  keyboard_ = GNEISS_KEYBOARD_STATE_INIT;
  pointer_.buttons = 0;
  pointer_.is_inside = 0;
  update_actions();
}

gneiss_result input_service::replace_action_map(action_map map) noexcept {
  std::vector<gneiss_action_state> states;
  try {
    states.assign(map.actions.size(), GNEISS_ACTION_STATE_INIT);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
  action_map_ = std::move(map);
  action_states_ = std::move(states);
  action_generation_ = next_action_generation.fetch_add(1, std::memory_order_relaxed);
  if (action_generation_ == 0U) {
    action_generation_ = next_action_generation.fetch_add(1, std::memory_order_relaxed);
  }
  update_actions();
  return GNEISS_SUCCESS;
}

gneiss_result input_service::find_action(std::string_view name,
                                         gneiss_action& out_action) const noexcept {
  out_action = GNEISS_NULL_ACTION;
  for (std::size_t index = 0; index < action_map_.actions.size(); ++index) {
    if (action_map_.actions[index].name == name) {
      out_action = (static_cast<std::uint64_t>(action_generation_) << 32U) | (index + 1U);
      return GNEISS_SUCCESS;
    }
  }
  return GNEISS_ERROR_NOT_FOUND;
}

gneiss_result input_service::get_action_state(gneiss_action action,
                                              gneiss_action_state& out_state) const noexcept {
  out_state = GNEISS_ACTION_STATE_INIT;
  const auto generation = static_cast<std::uint32_t>(action >> 32U);
  const auto slot = static_cast<std::uint32_t>(action);
  if (generation == 0U || generation != action_generation_ || slot == 0U ||
      slot > action_states_.size()) {
    return GNEISS_ERROR_INVALID_HANDLE;
  }
  out_state = action_states_[slot - 1U];
  return GNEISS_SUCCESS;
}

void input_service::update_actions() noexcept {
  for (std::size_t index = 0; index < action_map_.actions.size(); ++index) {
    auto& state = action_states_[index];
    const bool was_held = state.held != 0U;
    float value = 0.0F;
    for (const auto& binding : action_map_.actions[index].bindings) {
      const bool down = (keyboard_.pressed_keys[binding.physical_key / 64U] &
                         (UINT64_C(1) << (binding.physical_key % 64U))) != 0U;
      const float contribution = down ? binding.scale : 0.0F;
      if (std::abs(contribution) > std::abs(value)) {
        value = contribution;
      }
    }
    const bool is_held = value != 0.0F;
    state.pressed = static_cast<std::uint8_t>(state.pressed != 0U || (!was_held && is_held));
    state.released = static_cast<std::uint8_t>(state.released != 0U || (was_held && !is_held));
    state.held = static_cast<std::uint8_t>(is_held);
    state.value = std::clamp(value, -1.0F, 1.0F);
  }
}

void input_service::apply(const gneiss_input_event& event) noexcept {
  if (event.type == GNEISS_INPUT_EVENT_KEY && event.data.key.physical_key < 256U) {
    const auto key = event.data.key.physical_key;
    const auto mask = UINT64_C(1) << (key % 64U);
    auto& word = keyboard_.pressed_keys[key / 64U];
    if (event.data.key.action == GNEISS_KEY_RELEASED) {
      word &= ~mask;
    } else {
      word |= mask;
    }
    keyboard_.modifiers = event.data.key.modifiers;
  } else if (event.type == GNEISS_INPUT_EVENT_POINTER_MOVED) {
    pointer_.x = event.data.pointer_moved.x;
    pointer_.y = event.data.pointer_moved.y;
    pointer_.buttons = event.data.pointer_moved.buttons;
  } else if (event.type == GNEISS_INPUT_EVENT_POINTER_BUTTON) {
    pointer_.x = event.data.pointer_button.x;
    pointer_.y = event.data.pointer_button.y;
    pointer_.buttons = event.data.pointer_button.buttons;
  } else if (event.type == GNEISS_INPUT_EVENT_POINTER_ENTERED) {
    pointer_.is_inside = 1;
  } else if (event.type == GNEISS_INPUT_EVENT_POINTER_LEFT) {
    pointer_.is_inside = 0;
  }
}

} // namespace gneiss::input_internal
