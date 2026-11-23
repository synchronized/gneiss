// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/math/types.hpp>
#include <gneiss/engine/resource/texture.hpp>
#include <cstdint>
#include <variant>
namespace gneiss {
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
/** 借用目标 RID，提交帧时必须同时提供该目标的生产视图。 */
struct ui_target_image {
  rid target{};
};
using ui_image = std::variant<ref<const gneiss::texture>, ui_target_image>;
struct ui_draw_command {
  ui_image texture;
  math::vector2 clip_min{};
  math::vector2 clip_max{};
  std::uint32_t first_index{}, index_count{}, vertex_offset{};
};
} // namespace gneiss
