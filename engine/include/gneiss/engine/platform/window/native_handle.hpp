// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>

namespace gneiss::window {

enum class native_backend { none, win32, xcb, wayland };

/** 借用原生窗口；不负责释放。必须先销毁 Render 表面，再销毁所属窗口服务。 */
struct native_handle {
  native_backend backend{native_backend::none};
  void* display{};
  void* window{};
  std::uint32_t xcb_window{};
};

} // namespace gneiss::window
