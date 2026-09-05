// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_PLATFORM_NATIVE_WINDOW_INFO_H_
#define GNEISS_PLATFORM_NATIVE_WINDOW_INFO_H_

#include <cstdint>

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

} // namespace gneiss::application_internal

#endif
