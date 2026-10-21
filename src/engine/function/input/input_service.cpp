// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/input/input_service.hpp"

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
    state.pressed = false;
    state.released = false;
  }
}

bool input_service::push(const input_event& event) noexcept {
  if (event.type > event_type::pointer_left ||
      (event.type == event_type::text && event.data.text.length > sizeof(event.data.text.utf8)) ||
      (event.type == event_type::key && event.data.key.action > key_action::repeated) ||
      event_count_ == event_capacity) {
    return false;
  }
  events_[event_count_++] = event;
  apply(event);
  update_actions();
  return true;
}

result input_service::poll(input_event& out_event) noexcept {
  out_event = {};
  if (read_index_ == event_count_) {
    return result::not_ready;
  }
  out_event = events_[read_index_++];
  return result::success;
}

void input_service::clear_focus() noexcept {
  keyboard_ = {};
  pointer_.buttons = 0;
  pointer_.is_inside = false;
  update_actions();
}

result input_service::replace_action_map(engine::resource::action_map map) noexcept {
  if (const auto valid = validate_action_map(map); valid.failed()) {
    return valid;
  }
  std::vector<action_state> states;
  try {
    states.assign(map.actions.size(), action_state{});
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
  action_map_ = std::move(map);
  action_states_ = std::move(states);
  action_generation_ = next_action_generation.fetch_add(1, std::memory_order_relaxed);
  if (action_generation_ == 0U) {
    action_generation_ = next_action_generation.fetch_add(1, std::memory_order_relaxed);
  }
  update_actions();
  return result::success;
}

result input_service::find_action(std::string_view name, action_id& out_action) const noexcept {
  out_action = action_id{};
  for (std::size_t index = 0; index < action_map_.actions.size(); ++index) {
    if (action_map_.actions[index].name == name) {
      out_action = {(static_cast<std::uint64_t>(action_generation_) << 32U) | (index + 1U)};
      return result::success;
    }
  }
  return result::not_found;
}

result input_service::get_action_state(action_id action, action_state& out_state) const noexcept {
  out_state = action_state{};
  const auto generation = static_cast<std::uint32_t>(action.value >> 32U);
  const auto slot = static_cast<std::uint32_t>(action.value);
  if (generation == 0U || generation != action_generation_ || slot == 0U ||
      slot > action_states_.size()) {
    return result::invalid_handle;
  }
  out_state = action_states_[slot - 1U];
  return result::success;
}

void input_service::update_actions() noexcept {
  for (std::size_t index = 0; index < action_map_.actions.size(); ++index) {
    auto& state = action_states_[index];
    const bool was_held = state.held;
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
    state.pressed = state.pressed || (!was_held && is_held);
    state.released = state.released || (was_held && !is_held);
    state.held = is_held;
    state.value = std::clamp(value, -1.0F, 1.0F);
  }
}

void input_service::apply(const input_event& event) noexcept {
  if (event.type == event_type::key && event.data.key.physical_key < 256U) {
    const auto key = event.data.key.physical_key;
    const auto mask = UINT64_C(1) << (key % 64U);
    auto& word = keyboard_.pressed_keys[key / 64U];
    if (event.data.key.action == key_action::released) {
      word &= ~mask;
    } else {
      word |= mask;
    }
    keyboard_.modifiers = event.data.key.modifiers;
  } else if (event.type == event_type::pointer_moved) {
    pointer_.x = event.data.pointer_moved.x;
    pointer_.y = event.data.pointer_moved.y;
    pointer_.buttons = event.data.pointer_moved.buttons;
  } else if (event.type == event_type::pointer_button) {
    pointer_.x = event.data.pointer_button.x;
    pointer_.y = event.data.pointer_button.y;
    pointer_.buttons = event.data.pointer_button.buttons;
  } else if (event.type == event_type::pointer_entered) {
    pointer_.is_inside = true;
  } else if (event.type == event_type::pointer_left) {
    pointer_.is_inside = false;
  }
}

} // namespace gneiss::input_internal
