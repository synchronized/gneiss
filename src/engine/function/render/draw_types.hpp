// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/function/render/render_resource.hpp"
#include <cstdint>
namespace gneiss::engine {
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
  engine::texture texture;
  float clip_min[2]{};
  float clip_max[2]{};
  std::uint32_t first_index{}, index_count{}, vertex_offset{};
};
} // namespace gneiss::engine
