// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_H_
#define GNEISS_PLATFORM_GRANIT_GRANIT_PLATFORM_H_

#include <gneiss/application.h>

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
  [[nodiscard]] gneiss_result poll(bool& out_should_close) noexcept;
  [[nodiscard]] const native_window_info& native_window() const noexcept { return native_window_; }
  [[nodiscard]] native_window_info& native_window() noexcept { return native_window_; }

private:
  granit::window_system window_system_;
  granit::window window_;
  native_window_info native_window_;
};

} // namespace gneiss::application_internal

#endif
