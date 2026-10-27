// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/platform/window_service.hpp"
#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/result.hpp"

#include <granit/window.hpp>
#include <granit/window/native.hpp>

#include <cstring>
#include <string_view>

namespace gneiss::engine {
struct window_service::implementation {
  // 析构顺序：窗口先于窗口系统销毁；Application 先释放借用窗口的渲染服务。
  granit::window_system window_system_;
  granit::window window_;
  native_window_info native_window_;
};
window_service::window_service() : impl_(std::make_unique<implementation>()) {}
window_service::~window_service() = default;
const native_window_info& window_service::native_window() const noexcept {
  return impl_->native_window_;
}
native_window_info& window_service::native_window() noexcept { return impl_->native_window_; }

namespace {

engine::result map_result(granit::result result) noexcept {
  switch (result.native()) {
  case granit::result::success.native():
    return engine::result::success;
  case granit::result::invalid_argument.native():
    return engine::result::invalid_argument;
  case granit::result::invalid_handle.native():
    return engine::result::invalid_handle;
  case granit::result::out_of_memory.native():
    return engine::result::out_of_memory;
  case granit::result::unsupported.native():
  case granit::result::backend_unavailable.native():
    return engine::result::unsupported;
  case granit::result::not_ready.native():
    return engine::result::not_ready;
  case granit::result::initialization_failed.native():
  case granit::result::incompatible_driver.native():
    return engine::result::initialization_failed;
  default:
    return engine::result::dependency_failed;
  }
}

} // namespace

engine::result window_service::initialize(const window_configuration& config) noexcept {
  auto result = impl_->window_system_.initialize();
  if (result.failed()) {
    return map_result(result);
  }
  std::uint32_t flags = 0;
  flags |= config.visible ? GRANIT_WINDOW_VISIBLE_BIT : 0U;
  flags |= config.resizable ? GRANIT_WINDOW_RESIZABLE_BIT : 0U;
  flags |= config.high_dpi ? GRANIT_WINDOW_HIGH_DPI_BIT : 0U;
  result = impl_->window_.initialize(impl_->window_system_,
                                     {
                                         .title = config.title,
                                         .width = config.width,
                                         .height = config.height,
                                         .flags = static_cast<granit::window_flag>(flags),
                                     });
  if (result.failed()) {
    return map_result(result);
  }
  impl_->native_window_.width = config.width;
  impl_->native_window_.height = config.height;
  granit::window_native_win32 win32{};
  result = granit::get_native(impl_->window_system_, impl_->window_.ref(), win32);
  if (result.ok()) {
    impl_->native_window_.display = win32.instance;
    impl_->native_window_.window = win32.window;
    impl_->native_window_.backend = native_window_backend::win32;
    return engine::result::success;
  }
  granit::window_native_xcb xcb{};
  result = granit::get_native(impl_->window_system_, impl_->window_.ref(), xcb);
  if (result.ok()) {
    impl_->native_window_.display = xcb.connection;
    impl_->native_window_.xcb_window = xcb.window;
    impl_->native_window_.backend = native_window_backend::xcb;
    return engine::result::success;
  }
  granit::window_native_wayland wayland{};
  result = granit::get_native(impl_->window_system_, impl_->window_.ref(), wayland);
  if (result.ok()) {
    impl_->native_window_.display = wayland.display;
    impl_->native_window_.window = wayland.surface;
    impl_->native_window_.backend = native_window_backend::wayland;
    return engine::result::success;
  }
  return map_result(result);
}

engine::result window_service::poll_input(input_event& out_event) noexcept {
  granit::input_event source{};
  const auto result = impl_->window_system_.poll(source);
  if (result.failed()) {
    return map_result(result);
  }
  out_event = {};
  out_event.type = static_cast<event_type>(source.type);
  out_event.window_id = UINT64_C(1);
  out_event.timestamp_ns = source.timestamp_ns;
  switch (source.type) {
  case granit::input_event_type::key:
    out_event.data.key.physical_key = static_cast<std::uint32_t>(source.data.key.physical);
    out_event.data.key.logical_key = static_cast<std::uint32_t>(source.data.key.logical);
    out_event.data.key.modifiers = source.data.key.modifiers;
    out_event.data.key.action = static_cast<key_action>(source.data.key.action);
    break;
  case granit::input_event_type::text:
    static_assert(sizeof(out_event.data.text.utf8) == sizeof(source.data.text.utf8));
    if (source.data.text.length > sizeof(out_event.data.text.utf8)) {
      return engine::result::invalid_argument;
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
    out_event.type = event_type::none;
    break;
  }
  return engine::result::success;
}

engine::result window_service::keyboard(keyboard_state& out_state) const noexcept {
  granit::keyboard_state source{};
  const auto result = impl_->window_system_.keyboard(impl_->window_.ref(), source);
  if (result == granit::result::invalid_handle) {
    out_state = {};
    return engine::result::success;
  }
  if (result.failed()) {
    return map_result(result);
  }
  static_assert(sizeof(out_state.pressed_keys) == sizeof(source.pressed_keys));
  out_state = {};
  out_state.modifiers = source.modifiers;
  std::memcpy(out_state.pressed_keys.data(), source.pressed_keys.data(),
              sizeof(out_state.pressed_keys));
  return engine::result::success;
}

engine::result window_service::pointer(pointer_state& out_state) const noexcept {
  granit::pointer_state source{};
  const auto result = impl_->window_system_.pointer(impl_->window_.ref(), source);
  if (result == granit::result::invalid_handle) {
    out_state = {};
    return engine::result::success;
  }
  if (result.failed()) {
    return map_result(result);
  }
  out_state = {};
  out_state.buttons = source.buttons;
  out_state.x = source.x;
  out_state.y = source.y;
  out_state.is_inside = source.inside;
  return engine::result::success;
}

engine::result window_service::poll(bool& out_should_close, bool& out_focus_lost) noexcept {
  out_should_close = false;
  out_focus_lost = false;
  // 每轮只泵送一次，再依次消费窗口和输入队列。
  const auto pump_result = engine::measure(
      loop_stage::window_pump, [&]() noexcept { return impl_->window_system_.process_events(); });
  if (pump_result.failed()) {
    return map_result(pump_result);
  }
  const loop_span events(loop_stage::window_events);
  granit::window_event event{};
  auto result = impl_->window_system_.poll(event);
  while (result == granit::result::success) {
    if (event.type == granit::window_event_type::close_requested &&
        event.window == impl_->window_.ref()) {
      out_should_close = true;
    } else if (event.type == granit::window_event_type::focus_changed &&
               event.window == impl_->window_.ref() && !event.data.focus.focused) {
      out_focus_lost = true;
    } else if (event.type == granit::window_event_type::resized &&
               event.window == impl_->window_.ref()) {
      impl_->native_window_.width = event.data.resized.width;
      impl_->native_window_.height = event.data.resized.height;
      impl_->native_window_.needs_recreate = true;
    } else if (event.type == granit::window_event_type::scale_changed &&
               event.window == impl_->window_.ref()) {
      impl_->native_window_.width = event.data.scale.width;
      impl_->native_window_.height = event.data.scale.height;
      impl_->native_window_.needs_recreate = true;
    }
    event = {};
    result = impl_->window_system_.poll(event);
  }
  return result == granit::result::not_ready ? engine::result::success : map_result(result);
}

} // namespace gneiss::engine
