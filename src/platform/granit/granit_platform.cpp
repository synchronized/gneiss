// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "platform/granit/granit_platform.h"

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

gneiss_result granit_platform::poll(bool& out_should_close) noexcept {
  out_should_close = false;
  granit::window_event event = GRANIT_WINDOW_EVENT_INIT;
  auto result = window_system_.poll(event);
  while (result == granit::result::success) {
    if (event.type == GRANIT_WINDOW_EVENT_CLOSE_REQUESTED &&
        event.window == window_.native_handle()) {
      out_should_close = true;
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
