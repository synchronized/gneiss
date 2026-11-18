// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/platform/window/service.hpp>

#include <granit/window.hpp>
#include <granit/window/native.hpp>

#include <atomic>
#include <cstring>
#include <gneiss/engine/assert.hpp>
#include <string_view>

namespace gneiss::window {
struct service::implementation {
  // 析构顺序：窗口先于窗口系统销毁；Application 先释放借用窗口的渲染服务。
  granit::window_system window_system_;
  granit::window window_;
  window::id id_;
  native_handle native_window_;
  surface_state surface_;
  error refresh_surface() {
    granit::window_state state;
    const auto status = window_.get_state(state);
    if (status.failed())
      return error::dependency_failed;
    surface_ = {state.framebuffer_width, state.framebuffer_height, surface_.revision + 1};
    return error::success;
  }
};
service::service() : impl_(std::make_unique<implementation>()) {}
service::~service() = default;
window::id service::id() const noexcept { return impl_->id_; }
native_handle service::native() const noexcept { return impl_->native_window_; }
surface_state service::surface() const noexcept { return impl_->surface_; }

namespace {
std::atomic_uint64_t next_window_id{1};

gneiss::error map_result(granit::result result) noexcept {
  switch (result.native()) {
  case granit::result::success.native():
    return gneiss::error::success;
  case granit::result::invalid_argument.native():
    return gneiss::error::invalid_argument;
  case granit::result::invalid_handle.native():
    return gneiss::error::invalid_handle;
  case granit::result::out_of_memory.native():
    return gneiss::error::out_of_memory;
  case granit::result::unsupported.native():
  case granit::result::backend_unavailable.native():
    return gneiss::error::unsupported;
  case granit::result::not_ready.native():
    return gneiss::error::not_ready;
  case granit::result::initialization_failed.native():
  case granit::result::incompatible_driver.native():
    return gneiss::error::initialization_failed;
  default:
    return gneiss::error::dependency_failed;
  }
}

} // namespace

gneiss::error service::initialize(const configuration& config) noexcept {
  GNEISS_ASSERT(!impl_->id_, "窗口服务不能重复初始化");
  auto result = impl_->window_system_.initialize();
  if (result.failed()) {
    return map_result(result);
  }
  const auto flags =
      (config.visible ? granit::window_flag::visible : granit::window_flag::none) |
      (config.resizable ? granit::window_flag::resizable : granit::window_flag::none) |
      (config.high_dpi ? granit::window_flag::high_dpi : granit::window_flag::none);
  result = impl_->window_.initialize(impl_->window_system_, {
                                                                .title = config.title,
                                                                .width = config.width,
                                                                .height = config.height,
                                                                .flags = flags,
                                                            });
  if (result.failed()) {
    return map_result(result);
  }
  const auto refreshed = impl_->refresh_surface();
  if (refreshed.failed())
    return refreshed;
  granit::window_native native;
  result = granit::get_native(impl_->window_system_, impl_->window_.ref(), native);
  if (result.failed())
    return map_result(result);
  native_handle handle;
  switch (native.backend) {
  case GRANIT_WINDOW_BACKEND_WIN32:
    handle.backend = native_backend::win32;
    handle.data.win32 = {native.u.win32.instance, native.u.win32.window};
    break;
  case GRANIT_WINDOW_BACKEND_XCB:
    handle.backend = native_backend::xcb;
    handle.data.xcb = {native.u.xcb.connection, native.u.xcb.window};
    break;
  case GRANIT_WINDOW_BACKEND_WAYLAND:
    handle.backend = native_backend::wayland;
    handle.data.wayland = {native.u.wayland.display, native.u.wayland.surface};
    break;
  case GRANIT_WINDOW_BACKEND_EMSCRIPTEN:
    handle.backend = native_backend::emscripten;
    handle.data.emscripten = {native.u.emscripten.canvas_selector,
                              native.u.emscripten.canvas_selector_length};
    break;
  case GRANIT_WINDOW_BACKEND_SDL3:
    handle.backend = native_backend::sdl3;
    handle.data.sdl3 = {native.u.sdl3.window};
    break;
  default:
    return error::unsupported;
  }
  impl_->native_window_ = handle;
  impl_->id_ = {next_window_id.fetch_add(1, std::memory_order_relaxed)};
  GNEISS_ASSERT(impl_->id_, "窗口身份已耗尽");
  return error::success;
}

namespace {
error translate_input(const granit::input_event& source, window::id id, input::event& out_event) {
  out_event = {};
  out_event.type = static_cast<gneiss::input::event_type>(source.type);
  out_event.window_id = source.window ? id : window::id{};
  out_event.timestamp_ns = source.timestamp_ns;
  switch (source.type) {
  case granit::input_event_type::key:
    out_event.data.key.physical_key = static_cast<std::uint32_t>(source.data.key.physical);
    out_event.data.key.logical_key = static_cast<std::uint32_t>(source.data.key.logical);
    out_event.data.key.modifiers = source.data.key.modifiers;
    out_event.data.key.action = static_cast<gneiss::input::key_action>(source.data.key.action);
    break;
  case granit::input_event_type::text:
    static_assert(sizeof(out_event.data.text.utf8) == sizeof(source.data.text.utf8));
    if (source.data.text.length > sizeof(out_event.data.text.utf8)) {
      return gneiss::error::invalid_argument;
    }
    out_event.data.text.length = source.data.text.length;
    std::memcpy(out_event.data.text.utf8, source.data.text.utf8, sizeof(out_event.data.text.utf8));
    break;
  case granit::input_event_type::pointer_moved:
    out_event.data.pointer_moved.x = source.data.pointer_moved.x;
    out_event.data.pointer_moved.y = source.data.pointer_moved.y;
    out_event.data.pointer_moved.delta_x = source.data.pointer_moved.delta_x;
    out_event.data.pointer_moved.delta_y = source.data.pointer_moved.delta_y;
    out_event.data.pointer_moved.buttons = source.data.pointer_moved.buttons;
    break;
  case granit::input_event_type::pointer_button:
    out_event.data.pointer_button.x = source.data.pointer_button.x;
    out_event.data.pointer_button.y = source.data.pointer_button.y;
    out_event.data.pointer_button.button = source.data.pointer_button.button;
    out_event.data.pointer_button.pressed = source.data.pointer_button.pressed != 0;
    out_event.data.pointer_button.buttons = source.data.pointer_button.buttons;
    break;
  case granit::input_event_type::pointer_wheel:
    out_event.data.pointer_wheel.x = source.data.pointer_wheel.x;
    out_event.data.pointer_wheel.y = source.data.pointer_wheel.y;
    out_event.data.pointer_wheel.delta_x = source.data.pointer_wheel.delta_x;
    out_event.data.pointer_wheel.delta_y = source.data.pointer_wheel.delta_y;
    out_event.data.pointer_wheel.buttons = source.data.pointer_wheel.buttons;
    break;
  case granit::input_event_type::pointer_entered:
  case granit::input_event_type::pointer_left:
    break;
  default:
    out_event.type = gneiss::input::event_type::none;
    break;
  }
  return gneiss::error::success;
}

} // namespace

result<event_status>
service::poll_events(const std::function<error(const input::event&)>& on_input) {
  GNEISS_ASSERT(impl_->id_ && on_input);
  event_status status;
  // 每轮只泵送一次，再依次消费窗口和输入队列。
  const auto pump_result = impl_->window_system_.process_events();
  if (pump_result.failed()) {
    return map_result(pump_result);
  }
  granit::result result;
  {

    granit::window_event event{};
    result = impl_->window_system_.poll(event);
    while (result == granit::result::success) {
      if (event.type == granit::window_event_type::native_handle_changed &&
          event.window == impl_->window_.ref()) {
        // 原生句柄变化使旧 Surface 失效；尚未提供 Render 表面重新绑定，不能继续呈现。
        return error::unsupported;
      }
      if (event.type == granit::window_event_type::close_requested &&
          event.window == impl_->window_.ref()) {
        status.close_requested = true;
      } else if (event.type == granit::window_event_type::focus_changed &&
                 event.window == impl_->window_.ref() && !event.data.focus.focused) {
        status.focus_lost = true;
      } else if (event.type == granit::window_event_type::resized &&
                 event.window == impl_->window_.ref()) {
        const auto refreshed = impl_->refresh_surface();
        if (refreshed.failed())
          return refreshed;
      } else if (event.type == granit::window_event_type::scale_changed &&
                 event.window == impl_->window_.ref()) {
        const auto refreshed = impl_->refresh_surface();
        if (refreshed.failed())
          return refreshed;
      }
      event = {};
      result = impl_->window_system_.poll(event);
    }
    if (result != granit::result::not_ready)
      return map_result(result);
  }

  granit::input_event source{};
  while ((result = impl_->window_system_.poll(source)) == granit::result::success) {
    if (source.window && source.window != impl_->window_.ref())
      continue;
    input::event translated{};
    const auto converted = translate_input(source, impl_->id_, translated);
    if (converted.failed())
      return converted;
    const auto delivered = on_input(translated);
    if (delivered.failed())
      return delivered;
  }
  if (result != granit::result::not_ready)
    return map_result(result);
  return status;
}

} // namespace gneiss::window
