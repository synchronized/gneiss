// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstdint>
namespace gneiss::window {
enum class native_backend { none, win32, xcb, wayland, emscripten, sdl3 };

/** 借用原生窗口；backend 决定唯一有效的 data 分支。
 * 指针及 Canvas selector 仅在窗口存活期间有效；先销毁 Render 表面，再销毁窗口。
 * 表达窗口来源不代表当前 Render 后端支持该来源。 */
struct native_handle {
  native_backend backend{native_backend::none};
  union {
    struct {
      void* instance;
      void* window;
    } win32;
    struct {
      void* connection;
      std::uint32_t window;
    } xcb;
    struct {
      void* display;
      void* surface;
    } wayland;
    struct {
      const char* selector;
      std::uint32_t length;
    } emscripten;
    struct {
      void* window;
    } sdl3;
  } data{};
};
} // namespace gneiss::window
