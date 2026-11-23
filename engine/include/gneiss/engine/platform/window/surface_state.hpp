// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstdint>
namespace gneiss::window {
/** 窗口提供的只读表面快照；尺寸单位为 framebuffer 像素，零尺寸暂停窗口呈现。
 * revision 在所属窗口生命周期内递增，表示尺寸或表面条件变化，不是帧号。 */
struct surface_state {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t revision{};
};
} // namespace gneiss::window
