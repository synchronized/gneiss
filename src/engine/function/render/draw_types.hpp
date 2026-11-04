// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/types.hpp"
#include "engine/resource/texture/texture_resource.hpp"
#include <cstdint>
namespace gneiss::engine {
struct debug_line {
  math::vector3 start{};
  math::vector3 end{};
  std::uint32_t color_rgba8{};
  float width{1.0F};
  bool depth_test{true};
};
struct ui_vertex {
  math::vector2 position{};
  math::vector2 uv{};
  std::uint32_t color_rgba8{};
};
struct ui_draw_command {
  engine::texture texture;
  math::vector2 clip_min{};
  math::vector2 clip_max{};
  std::uint32_t first_index{}, index_count{}, vertex_offset{};
};
} // namespace gneiss::engine
