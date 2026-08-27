// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "platform/granit/granit_platform.h"

#include <cstring>
#include <string_view>

namespace gneiss::application_internal {
namespace {

gneiss_result map_result(granit::result result) noexcept {
  switch (result) {
  case granit::result::success:
    return GNEISS_SUCCESS;
  case granit::result::invalid_argument:
    return GNEISS_ERROR_INVALID_ARGUMENT;
  case granit::result::invalid_handle:
    return GNEISS_ERROR_INVALID_HANDLE;
  case granit::result::out_of_memory:
    return GNEISS_ERROR_OUT_OF_MEMORY;
  case granit::result::unsupported:
  case granit::result::backend_unavailable:
    return GNEISS_ERROR_UNSUPPORTED;
  case granit::result::not_ready:
    return GNEISS_ERROR_NOT_READY;
  case granit::result::initialization_failed:
  case granit::result::incompatible_driver:
    return GNEISS_ERROR_INITIALIZATION_FAILED;
  default:
    return GNEISS_ERROR_DEPENDENCY_FAILED;
  }
}

} // namespace

gneiss_result granit_platform::initialize(const gneiss_application_desc& desc) noexcept {
  auto result = window_system_.initialize();
  if (granit::failed(result)) {
    return map_result(result);
  }
  std::uint32_t flags = 0;
  flags |= (desc.window_flags & GNEISS_APPLICATION_WINDOW_VISIBLE_BIT) != 0U
               ? GRANIT_WINDOW_VISIBLE_BIT
               : 0U;
  flags |= (desc.window_flags & GNEISS_APPLICATION_WINDOW_RESIZABLE_BIT) != 0U
               ? GRANIT_WINDOW_RESIZABLE_BIT
               : 0U;
  flags |= (desc.window_flags & GNEISS_APPLICATION_WINDOW_HIGH_DPI_BIT) != 0U
               ? GRANIT_WINDOW_HIGH_DPI_BIT
               : 0U;
  const auto title = desc.window_title_length == 0U
                         ? std::string_view{"Gneiss"}
                         : std::string_view{desc.window_title, desc.window_title_length};
  result = window_.initialize(
      window_system_.native_handle(),
      {.title = title, .width = desc.window_width, .height = desc.window_height, .flags = flags});
  if (granit::failed(result)) {
    return map_result(result);
  }
  result = input_system_.initialize(window_system_.native_handle());
  if (granit::failed(result)) {
    return map_result(result);
  }

  native_window_.width = desc.window_width;
  native_window_.height = desc.window_height;
  result = window_.native_win32(native_window_.display, native_window_.window);
  if (granit::succeeded(result)) {
    native_window_.backend = native_window_backend::win32;
    return GNEISS_SUCCESS;
  }
  result = window_.native_xcb(native_window_.display, native_window_.xcb_window);
  if (granit::succeeded(result)) {
    native_window_.backend = native_window_backend::xcb;
    return GNEISS_SUCCESS;
  }
  result = window_.native_wayland(native_window_.display, native_window_.window);
  if (granit::succeeded(result)) {
    native_window_.backend = native_window_backend::wayland;
    return GNEISS_SUCCESS;
  }
  return map_result(result);
}

gneiss_result granit_platform::poll_input(gneiss_input_event& out_event) noexcept {
  granit::input_event source = GRANIT_INPUT_EVENT_INIT;
  const auto result = input_system_.poll(source);
  if (granit::failed(result)) {
    return map_result(result);
  }
  out_event = GNEISS_INPUT_EVENT_INIT;
  out_event.type = source.type;
  out_event.window_id = UINT64_C(1);
  out_event.timestamp_ns = source.timestamp_ns;
  switch (source.type) {
  case GRANIT_INPUT_EVENT_KEY:
    out_event.data.key.physical_key = source.data.key.physical_key;
    out_event.data.key.logical_key = source.data.key.logical_key;
    out_event.data.key.modifiers = source.data.key.modifiers;
    out_event.data.key.action = source.data.key.action;
    break;
  case GRANIT_INPUT_EVENT_TEXT:
    out_event.data.text.length = source.data.text.length;
    std::memcpy(out_event.data.text.utf8, source.data.text.utf8, sizeof(out_event.data.text.utf8));
    break;
  case GRANIT_INPUT_EVENT_POINTER_MOVED:
    out_event.data.pointer_moved.x = source.data.pointer_moved.x;
    out_event.data.pointer_moved.y = source.data.pointer_moved.y;
    out_event.data.pointer_moved.delta_x = source.data.pointer_moved.delta_x;
    out_event.data.pointer_moved.delta_y = source.data.pointer_moved.delta_y;
    out_event.data.pointer_moved.buttons = source.data.pointer_moved.buttons;
    break;
  case GRANIT_INPUT_EVENT_POINTER_BUTTON:
    out_event.data.pointer_button.x = source.data.pointer_button.x;
    out_event.data.pointer_button.y = source.data.pointer_button.y;
    out_event.data.pointer_button.button = source.data.pointer_button.button;
    out_event.data.pointer_button.pressed = source.data.pointer_button.pressed;
    out_event.data.pointer_button.buttons = source.data.pointer_button.buttons;
    break;
  case GRANIT_INPUT_EVENT_POINTER_WHEEL:
    out_event.data.pointer_wheel.x = source.data.pointer_wheel.x;
    out_event.data.pointer_wheel.y = source.data.pointer_wheel.y;
    out_event.data.pointer_wheel.delta_x = source.data.pointer_wheel.delta_x;
    out_event.data.pointer_wheel.delta_y = source.data.pointer_wheel.delta_y;
    out_event.data.pointer_wheel.buttons = source.data.pointer_wheel.buttons;
    break;
  case GRANIT_INPUT_EVENT_POINTER_ENTERED:
  case GRANIT_INPUT_EVENT_POINTER_LEFT:
    break;
  default:
    out_event.type = 0;
    break;
  }
  return GNEISS_SUCCESS;
}

gneiss_result granit_platform::keyboard(gneiss_keyboard_state& out_state) const noexcept {
  granit::keyboard_state source = GRANIT_KEYBOARD_STATE_INIT;
  const auto result = input_system_.keyboard(window_.native_handle(), source);
  if (result == granit::result::invalid_handle) {
    out_state = GNEISS_KEYBOARD_STATE_INIT;
    return GNEISS_SUCCESS;
  }
  if (granit::failed(result)) {
    return map_result(result);
  }
  static_assert(sizeof(out_state.pressed_keys) == sizeof(source.pressed_keys));
  out_state = GNEISS_KEYBOARD_STATE_INIT;
  out_state.modifiers = source.modifiers;
  std::memcpy(out_state.pressed_keys, source.pressed_keys, sizeof(out_state.pressed_keys));
  return GNEISS_SUCCESS;
}

gneiss_result granit_platform::pointer(gneiss_pointer_state& out_state) const noexcept {
  granit::pointer_state source = GRANIT_POINTER_STATE_INIT;
  const auto result = input_system_.pointer(window_.native_handle(), source);
  if (result == granit::result::invalid_handle) {
    out_state = GNEISS_POINTER_STATE_INIT;
    return GNEISS_SUCCESS;
  }
  if (granit::failed(result)) {
    return map_result(result);
  }
  out_state = GNEISS_POINTER_STATE_INIT;
  out_state.buttons = source.buttons;
  out_state.x = source.x;
  out_state.y = source.y;
  out_state.is_inside = source.inside;
  return GNEISS_SUCCESS;
}

gneiss_result granit_platform::poll(bool& out_should_close, bool& out_focus_lost) noexcept {
  out_should_close = false;
  out_focus_lost = false;
  granit::window_event event = GRANIT_WINDOW_EVENT_INIT;
  auto result = window_system_.poll(event);
  while (result == granit::result::success) {
    if (event.type == GRANIT_WINDOW_EVENT_CLOSE_REQUESTED &&
        event.window == window_.native_handle()) {
      out_should_close = true;
    } else if (event.type == GRANIT_WINDOW_EVENT_FOCUS_CHANGED &&
               event.window == window_.native_handle() && event.data.focus.focused == 0U) {
      out_focus_lost = true;
    } else if (event.type == GRANIT_WINDOW_EVENT_RESIZED &&
               event.window == window_.native_handle()) {
      native_window_.width = event.data.resized.width;
      native_window_.height = event.data.resized.height;
      native_window_.needs_recreate = true;
    } else if (event.type == GRANIT_WINDOW_EVENT_SCALE_CHANGED &&
               event.window == window_.native_handle()) {
      native_window_.width = event.data.scale.width;
      native_window_.height = event.data.scale.height;
      native_window_.needs_recreate = true;
    }
    event = GRANIT_WINDOW_EVENT_INIT;
    result = window_system_.poll(event);
  }
  return result == granit::result::not_ready ? GNEISS_SUCCESS : map_result(result);
}

} // namespace gneiss::application_internal
