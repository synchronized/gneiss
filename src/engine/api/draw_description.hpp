// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/function/render/ui_draw_list.hpp"

namespace gneiss::api {

/** 解析 ABI 布局与同步借用范围，失败保留输出；绘制语义由 Render 校验。 */
[[nodiscard]] inline gneiss_result
read_ui_draw_description(const gneiss_ui_draw_list_desc& desc,
                         render_internal::ui_draw_view& output) noexcept {
  if (desc.struct_size < GNEISS_UI_DRAW_LIST_DESC_VERSION_1_SIZE || desc.reserved != 0U ||
      desc.reserved_2 != 0U || (desc.vertex_count != 0U && desc.vertices == nullptr) ||
      (desc.index_count != 0U && desc.indices == nullptr) ||
      (desc.command_count != 0U && desc.commands == nullptr)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  output = {
      .display_width = desc.display_width,
      .display_height = desc.display_height,
      .framebuffer_scale_x = desc.framebuffer_scale_x,
      .framebuffer_scale_y = desc.framebuffer_scale_y,
      .vertices = {desc.vertices, desc.vertex_count},
      .indices = {desc.indices, desc.index_count},
      .commands = {desc.commands, desc.command_count},
  };
  return GNEISS_SUCCESS;
}

[[nodiscard]] inline gneiss_result
read_debug_draw_description(const gneiss_debug_draw_list_desc& desc,
                            std::span<const gneiss_debug_line>& output) noexcept {
  if (desc.struct_size < GNEISS_DEBUG_DRAW_LIST_DESC_VERSION_1_SIZE || desc.reserved != 0U ||
      desc.reserved_2 != 0U || (desc.line_count != 0U && desc.lines == nullptr)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  output = {desc.lines, desc.line_count};
  return GNEISS_SUCCESS;
}

} // namespace gneiss::api
