// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <cstdint>
namespace gneiss::render_internal {
struct debug_line {
  float start[3]{};
  float end[3]{};
  std::uint32_t color_rgba8{};
  float width{1.0F};
  bool depth_test{true};
};
struct ui_vertex {
  float position[2]{};
  float uv[2]{};
  std::uint32_t color_rgba8{};
};
struct ui_draw_command {
  std::uint64_t texture{};
  float clip_min[2]{};
  float clip_max[2]{};
  std::uint32_t first_index{}, index_count{}, vertex_offset{};
};
} // namespace gneiss::render_internal
