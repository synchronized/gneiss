// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include <algorithm>
#include <gneiss/engine/render.h>

namespace gneiss::engine::api {
/** ABI 转换期间拥有原生数组，view 仅在此对象存活时借用。 */
struct converted_ui_draw {
  converted_ui_draw() = default;
  converted_ui_draw(const converted_ui_draw&) = delete;
  converted_ui_draw& operator=(const converted_ui_draw&) = delete;
  converted_ui_draw(converted_ui_draw&&) = default;
  converted_ui_draw& operator=(converted_ui_draw&&) = default;
  std::vector<engine::render::ui_vertex> vertices;
  std::vector<engine::render::ui_draw_command> commands;
  engine::render::ui_draw_view view;
};

/** 解析 ABI 布局与同步借用范围，失败保留输出；绘制语义由 Render 校验。 */
[[nodiscard]] inline gneiss_result read_ui_draw_description(const gneiss_ui_draw_list_desc& desc,
                                                            converted_ui_draw& output) {
  if (desc.struct_size < GNEISS_UI_DRAW_LIST_DESC_VERSION_1_SIZE || desc.reserved != 0U ||
      desc.reserved_2 != 0U || (desc.vertex_count != 0U && desc.vertices == nullptr) ||
      (desc.index_count != 0U && desc.indices == nullptr) ||
      (desc.command_count != 0U && desc.commands == nullptr) ||
      desc.vertex_count > 16U * 1024U * 1024U || desc.index_count > 32U * 1024U * 1024U ||
      desc.command_count > 1024U * 1024U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  converted_ui_draw candidate;
  candidate.vertices.reserve(desc.vertex_count);
  candidate.commands.reserve(desc.command_count);
  for (std::uint32_t index = 0; index < desc.vertex_count; ++index) {
    const auto& value = desc.vertices[index];
    candidate.vertices.push_back(
        {{value.position[0], value.position[1]}, {value.uv[0], value.uv[1]}, value.color_rgba8});
  }
  for (std::uint32_t index = 0; index < desc.command_count; ++index) {
    const auto& value = desc.commands[index];
    if (value.reserved != 0U)
      return GNEISS_ERROR_INVALID_ARGUMENT;
    candidate.commands.push_back({value.texture,
                                  {value.clip_min[0], value.clip_min[1]},
                                  {value.clip_max[0], value.clip_max[1]},
                                  value.first_index,
                                  value.index_count,
                                  value.vertex_offset});
  }
  candidate.view = {
      .display_width = desc.display_width,
      .display_height = desc.display_height,
      .framebuffer_scale_x = desc.framebuffer_scale_x,
      .framebuffer_scale_y = desc.framebuffer_scale_y,
      .vertices = candidate.vertices,
      .indices = {desc.indices, desc.index_count},
      .commands = candidate.commands,
  };
  output = std::move(candidate);
  return GNEISS_SUCCESS;
}

[[nodiscard]] inline gneiss_result
read_debug_draw_description(const gneiss_debug_draw_list_desc& desc,
                            std::vector<engine::render::debug_line>& output) {
  if (desc.struct_size < GNEISS_DEBUG_DRAW_LIST_DESC_VERSION_1_SIZE || desc.reserved != 0U ||
      desc.reserved_2 != 0U || (desc.line_count != 0U && desc.lines == nullptr) ||
      desc.line_count > 1024U * 1024U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  std::vector<engine::render::debug_line> candidate;
  candidate.reserve(desc.line_count);
  for (std::uint32_t index = 0; index < desc.line_count; ++index) {
    const auto& value = desc.lines[index];
    if (value.depth_test > 1U || value.reserved[0] || value.reserved[1] || value.reserved[2]) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    candidate.push_back({{value.start[0], value.start[1], value.start[2]},
                         {value.end[0], value.end[1], value.end[2]},
                         value.color_rgba8,
                         value.width,
                         value.depth_test != 0U});
  }
  output = std::move(candidate);
  return GNEISS_SUCCESS;
}

} // namespace gneiss::engine::api
