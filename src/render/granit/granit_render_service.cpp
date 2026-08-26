// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/granit_render_service.h"

#include <span>

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
  case granit::result::out_of_date:
    return GNEISS_ERROR_NOT_READY;
  case granit::result::initialization_failed:
  case granit::result::incompatible_driver:
    return GNEISS_ERROR_INITIALIZATION_FAILED;
  default:
    return GNEISS_ERROR_DEPENDENCY_FAILED;
  }
}

granit::surface_type to_surface_type(native_window_backend backend) noexcept {
  switch (backend) {
  case native_window_backend::win32:
    return granit::surface_type::win32;
  case native_window_backend::xcb:
    return granit::surface_type::xcb;
  case native_window_backend::wayland:
    return granit::surface_type::wayland;
  default:
    return granit::surface_type::none;
  }
}

} // namespace

gneiss_result granit_render_service::initialize(const native_window_info& window) noexcept {
  auto result = renderer_.initialize({.application_name = "Gneiss",
                                      .enable_validation = false,
                                      .surface_types = to_surface_type(window.backend)});
  if (granit::failed(result)) {
    return map_result(result);
  }

  switch (window.backend) {
  case native_window_backend::win32:
    result = surface_.initialize_win32(renderer_.native_handle(),
                                       {.instance = window.display, .window = window.window});
    break;
  case native_window_backend::xcb:
    result = surface_.initialize_xcb(renderer_.native_handle(),
                                     {.connection = window.display, .window = window.xcb_window});
    break;
  case native_window_backend::wayland:
    result = surface_.initialize_wayland(renderer_.native_handle(),
                                         {.display = window.display, .surface = window.window});
    break;
  default:
    return GNEISS_ERROR_UNSUPPORTED;
  }
  if (granit::succeeded(result)) {
    result = swapchain_.initialize(renderer_.native_handle(), surface_.native_handle(),
                                   {.width = window.width, .height = window.height});
  }
  if (granit::succeeded(result)) {
    result = frame_context_.initialize(renderer_.native_handle());
  }
  return map_result(result);
}

gneiss_result granit_render_service::render(native_window_info& window) noexcept {
  if (window.width == 0U || window.height == 0U) {
    return GNEISS_SUCCESS;
  }
  if (window.needs_recreate) {
    const auto recreate_result =
        swapchain_.recreate({.width = window.width, .height = window.height});
    if (recreate_result == granit::result::not_ready) {
      return GNEISS_SUCCESS;
    }
    if (granit::failed(recreate_result)) {
      return map_result(recreate_result);
    }
    window.needs_recreate = false;
  }

  granit::acquired_frame frame;
  auto result = swapchain_.acquire(frame);
  if (result == granit::result::out_of_date) {
    window.needs_recreate = true;
    return GNEISS_SUCCESS;
  }
  if (granit::failed(result)) {
    return map_result(result);
  }
  window.needs_recreate = window.needs_recreate || frame.needs_recreate;

  granit_texture texture = GRANIT_NULL_HANDLE;
  granit_texture_view view = GRANIT_NULL_HANDLE;
  result = swapchain_.backbuffer(frame.image_index, texture, view);
  granit::frame_recording recording;
  if (granit::succeeded(result)) {
    result = frame_context_.begin(frame, recording);
  }
  if (granit::succeeded(result)) {
    const granit::color_attachment_desc color{
        .view = view, .clear_value = {.red = 0.04F, .green = 0.12F, .blue = 0.22F, .alpha = 1.0F}};
    const granit::rendering_desc rendering{
        .color_attachments = std::span{&color, 1},
        .area = {.x = 0, .y = 0, .width = window.width, .height = window.height}};
    result = recording.recorder().begin_rendering(rendering);
    if (granit::succeeded(result)) {
      result = recording.recorder().end_rendering();
    }
  }
  if (granit::succeeded(result)) {
    result = recording.submit();
  }
  if (granit::succeeded(result)) {
    result = swapchain_.present(frame);
  }
  window.needs_recreate = window.needs_recreate || frame.needs_recreate;
  if (result == granit::result::out_of_date) {
    window.needs_recreate = true;
    result = granit::result::success;
  }
  if (granit::failed(result)) {
    if (recording.valid()) {
      static_cast<void>(recording.abort());
    }
    if (frame.valid()) {
      static_cast<void>(swapchain_.cancel(frame));
    }
  }
  return map_result(result);
}

} // namespace gneiss::application_internal
