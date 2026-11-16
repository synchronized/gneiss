// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <cstdint>

namespace gneiss {

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

} // namespace gneiss
