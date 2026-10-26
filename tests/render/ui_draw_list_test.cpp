// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/render/ui_draw_list.hpp"

#include <array>
#include <cstdint>

int main() {
  gneiss::engine::render_resource_service resources;
  constexpr std::array<std::uint8_t, 4> pixels{255U, 255U, 255U, 255U};
  gneiss::engine::texture_view texture_desc;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixels = pixels;
  gneiss::engine::texture texture;
  if (resources.create_texture(texture_desc, texture) != gneiss::engine::result::success) {
    return 1;
  }

  std::array vertices{gneiss::engine::ui_vertex{{0.0F, 0.0F}, {0.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss::engine::ui_vertex{{1.0F, 0.0F}, {1.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss::engine::ui_vertex{{0.0F, 1.0F}, {0.0F, 1.0F}, UINT32_C(0xffffffff)}};
  std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
  std::array commands{gneiss::engine::ui_draw_command{.texture = texture,
                                                      .clip_min = {0.0F, 0.0F},
                                                      .clip_max = {640.0F, 480.0F},
                                                      .first_index = 0U,
                                                      .index_count = 3U,
                                                      .vertex_offset = 0U}};
  gneiss::engine::ui_draw_view desc;
  desc.display_width = 640.0F;
  desc.display_height = 480.0F;
  desc.vertices = vertices;
  desc.indices = indices;
  desc.commands = commands;

  gneiss::engine::ui_draw_list draw_list;
  if (draw_list.replace(desc, resources) != gneiss::engine::result::success ||
      draw_list.vertices().size() != 3U || draw_list.indices().size() != 3U ||
      draw_list.commands().size() != 1U) {
    return 2;
  }
  vertices[0].position[0] = 9.0F;
  indices[0] = 2U;
  commands[0].index_count = 0U;
  if (draw_list.vertices()[0].position[0] != 0.0F || draw_list.indices()[0] != 0U ||
      draw_list.commands()[0].index_count != 3U) {
    return 3;
  }

  indices[0] = 3U;
  commands[0].index_count = 3U;
  if (draw_list.replace(desc, resources) != gneiss::engine::result::invalid_argument ||
      draw_list.indices()[0] != 0U) {
    return 4;
  }
  indices[0] = 0U;

  commands[0].index_count = 2U;
  if (draw_list.replace(desc, resources) != gneiss::engine::result::invalid_argument ||
      draw_list.commands()[0].index_count != 3U) {
    return 5;
  }
  commands[0].index_count = 3U;

  gneiss::engine::render_resource_service other_resources;
  if (draw_list.replace(desc, other_resources) != gneiss::engine::result::invalid_handle ||
      draw_list.commands().size() != 1U) {
    return 6;
  }

  draw_list.clear();
  if (!draw_list.vertices().empty() || !draw_list.indices().empty() ||
      !draw_list.commands().empty() || draw_list.display_width() != 0.0F) {
    return 7;
  }
  gneiss::engine::texture rejected = texture;
  if (resources.create_texture({}, rejected) != gneiss::engine::result::invalid_argument ||
      rejected || !texture)
    return 8;
  return 0;
}
