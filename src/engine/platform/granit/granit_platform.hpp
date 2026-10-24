// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_HPP_
#define GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_HPP_

#include "engine/platform/native_window_info.hpp"

#include "engine/core/input/input_types.hpp"
#include "engine/core/result.hpp"
#include "engine/platform/window_configuration.hpp"

#include <granit/window.hpp>

namespace gneiss::engine::platform {

class granit_platform final {
public:
  [[nodiscard]] gneiss::engine::core::result
  initialize(const window_configuration& config) noexcept;
  [[nodiscard]] gneiss::engine::core::result poll(bool& out_should_close,
                                                  bool& out_focus_lost) noexcept;
  [[nodiscard]] gneiss::engine::core::result
  poll_input(engine::core::input::input_event& out_event) noexcept;
  [[nodiscard]] gneiss::engine::core::result
  keyboard(engine::core::input::keyboard_state& out_state) const noexcept;
  [[nodiscard]] gneiss::engine::core::result
  pointer(engine::core::input::pointer_state& out_state) const noexcept;
  [[nodiscard]] const native_window_info& native_window() const noexcept { return native_window_; }
  [[nodiscard]] native_window_info& native_window() noexcept { return native_window_; }

private:
  granit::window_system window_system_;
  granit::window window_;
  native_window_info native_window_;
};

} // namespace gneiss::engine::platform

#endif
