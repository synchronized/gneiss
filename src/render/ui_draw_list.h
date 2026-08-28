// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_UI_DRAW_LIST_H_
#define GNEISS_RENDER_UI_DRAW_LIST_H_

#include "render/render_resource_service.h"

#include <gneiss/render.h>

#include <vector>

namespace gneiss::render_internal {

class ui_draw_list final {
public:
  [[nodiscard]] gneiss_result replace(const gneiss_ui_draw_list_desc& desc,
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
