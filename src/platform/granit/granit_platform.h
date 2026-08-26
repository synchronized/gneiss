// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_H_
#define GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_H_

#include <gneiss/application.h>
#include <gneiss/input.h>

#include <granit/input/input.hpp>
#include <granit/window.hpp>

namespace gneiss::application_internal {

enum class native_window_backend { none, win32, xcb, wayland };

struct native_window_info {
  native_window_backend backend{native_window_backend::none};
  void* display{};
  void* window{};
  std::uint32_t xcb_window{};
  std::uint32_t width{};
  std::uint32_t height{};
  bool needs_recreate{};
};

class granit_platform final {
public:
  [[nodiscard]] gneiss_result initialize(const gneiss_application_desc& desc) noexcept;
  [[nodiscard]] gneiss_result poll(bool& out_should_close, bool& out_focus_lost) noexcept;
  [[nodiscard]] gneiss_result poll_input(gneiss_input_event& out_event) noexcept;
  [[nodiscard]] gneiss_result keyboard(gneiss_keyboard_state& out_state) const noexcept;
  [[nodiscard]] gneiss_result pointer(gneiss_pointer_state& out_state) const noexcept;
  [[nodiscard]] const native_window_info& native_window() const noexcept { return native_window_; }
  [[nodiscard]] native_window_info& native_window() noexcept { return native_window_; }

private:
  granit::window_system window_system_;
  granit::window window_;
  granit::input_system input_system_;
  native_window_info native_window_;
};

} // namespace gneiss::application_internal

#endif
