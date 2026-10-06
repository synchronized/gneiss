// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_UI_DRAW_LIST_HPP_
#define GNEISS_RENDER_UI_DRAW_LIST_HPP_

#include "engine/function/render/render_resource_service.hpp"

#include <gneiss/engine/render.h>

#include <span>
#include <vector>

namespace gneiss::render_internal {

/** 同步提交借用；成功后绘制列表拥有数组副本，失败保留上一份列表。 */
struct ui_draw_view {
  float display_width{};
  float display_height{};
  float framebuffer_scale_x{1.0F};
  float framebuffer_scale_y{1.0F};
  std::span<const gneiss_ui_vertex> vertices;
  std::span<const std::uint32_t> indices;
  std::span<const gneiss_ui_draw_command> commands;
};

class ui_draw_list final {
public:
  [[nodiscard]] gneiss_result replace(const ui_draw_view& desc,
                                      const render_resource_service& resources) noexcept;
  void clear() noexcept;

  [[nodiscard]] float display_width() const noexcept { return display_width_; }
  [[nodiscard]] float display_height() const noexcept { return display_height_; }
  [[nodiscard]] float framebuffer_scale_x() const noexcept { return framebuffer_scale_x_; }
  [[nodiscard]] float framebuffer_scale_y() const noexcept { return framebuffer_scale_y_; }
  [[nodiscard]] const std::vector<gneiss_ui_vertex>& vertices() const noexcept { return vertices_; }
  [[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept { return indices_; }
  [[nodiscard]] const std::vector<gneiss_ui_draw_command>& commands() const noexcept {
    return commands_;
  }

private:
  float display_width_{};
  float display_height_{};
  float framebuffer_scale_x_{1.0F};
  float framebuffer_scale_y_{1.0F};
  std::vector<gneiss_ui_vertex> vertices_;
  std::vector<std::uint32_t> indices_;
  std::vector<gneiss_ui_draw_command> commands_;
};

} // namespace gneiss::render_internal

#endif
