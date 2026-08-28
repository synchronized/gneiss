// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/ui_draw_list.h"

#include <array>
#include <cstdint>

int main() {
  gneiss::render_internal::render_resource_service resources;
  constexpr std::array<std::uint8_t, 4> pixels{255U, 255U, 255U, 255U};
  gneiss_texture_desc texture_desc = GNEISS_TEXTURE_DESC_INIT;
  texture_desc.width = 1U;
  texture_desc.height = 1U;
  texture_desc.row_stride_bytes = 4U;
  texture_desc.pixel_data_size = pixels.size();
  texture_desc.pixels = pixels.data();
  gneiss_texture texture = GNEISS_NULL_TEXTURE;
  if (resources.create_texture(texture_desc, &texture) != GNEISS_SUCCESS) {
    return 1;
  }

  std::array vertices{gneiss_ui_vertex{{0.0F, 0.0F}, {0.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss_ui_vertex{{1.0F, 0.0F}, {1.0F, 0.0F}, UINT32_C(0xffffffff)},
                      gneiss_ui_vertex{{0.0F, 1.0F}, {0.0F, 1.0F}, UINT32_C(0xffffffff)}};
  std::array<std::uint32_t, 3> indices{0U, 1U, 2U};
  std::array commands{gneiss_ui_draw_command{.texture = texture,
                                             .clip_min = {0.0F, 0.0F},
                                             .clip_max = {640.0F, 480.0F},
                                             .first_index = 0U,
                                             .index_count = 3U,
                                             .vertex_offset = 0U,
                                             .reserved = 0U}};
  gneiss_ui_draw_list_desc desc = GNEISS_UI_DRAW_LIST_DESC_INIT;
  desc.display_width = 640.0F;
  desc.display_height = 480.0F;
  desc.vertex_count = static_cast<std::uint32_t>(vertices.size());
  desc.vertices = vertices.data();
  desc.index_count = static_cast<std::uint32_t>(indices.size());
  desc.indices = indices.data();
  desc.command_count = static_cast<std::uint32_t>(commands.size());
  desc.commands = commands.data();

  gneiss::render_internal::ui_draw_list draw_list;
  if (draw_list.replace(desc, resources) != GNEISS_SUCCESS || draw_list.vertices().size() != 3U ||
      draw_list.indices().size() != 3U || draw_list.commands().size() != 1U) {
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
  if (draw_list.replace(desc, resources) != GNEISS_ERROR_INVALID_ARGUMENT ||
      draw_list.indices()[0] != 0U) {
    return 4;
  }
  indices[0] = 0U;

  commands[0].index_count = 2U;
  if (draw_list.replace(desc, resources) != GNEISS_ERROR_INVALID_ARGUMENT ||
      draw_list.commands()[0].index_count != 3U) {
    return 5;
  }
  commands[0].index_count = 3U;

  gneiss::render_internal::render_resource_service other_resources;
  if (draw_list.replace(desc, other_resources) != GNEISS_ERROR_INVALID_HANDLE ||
      draw_list.commands().size() != 1U) {
    return 6;
  }

  draw_list.clear();
  if (!draw_list.vertices().empty() || !draw_list.indices().empty() ||
      !draw_list.commands().empty() || draw_list.display_width() != 0.0F) {
    return 7;
  }
  return 0;
}
