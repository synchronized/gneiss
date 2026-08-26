// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "input/input_service.h"

namespace gneiss::input_internal {

void input_service::begin_frame() noexcept {
  read_index_ = 0;
  event_count_ = 0;
}

bool input_service::push(const gneiss_input_event& event) noexcept {
  if (event_count_ == event_capacity) {
    return false;
  }
  events_[event_count_++] = event;
  apply(event);
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
