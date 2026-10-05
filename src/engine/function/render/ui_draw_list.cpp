// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/ui_draw_list.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <new>

namespace gneiss::render_internal {
namespace {

constexpr std::uint32_t maximum_vertices = UINT32_C(16) * 1024U * 1024U;
constexpr std::uint32_t maximum_indices = UINT32_C(32) * 1024U * 1024U;
constexpr std::uint32_t maximum_commands = UINT32_C(1024) * 1024U;

bool is_finite(float value) noexcept { return std::isfinite(value); }

} // namespace

gneiss_result ui_draw_list::replace(const ui_draw_view& desc,
                                    const render_resource_service& resources) noexcept {
  if (!is_finite(desc.display_width) || desc.display_width <= 0.0F ||
      !is_finite(desc.display_height) || desc.display_height <= 0.0F ||
      !is_finite(desc.framebuffer_scale_x) || desc.framebuffer_scale_x <= 0.0F ||
      !is_finite(desc.framebuffer_scale_y) || desc.framebuffer_scale_y <= 0.0F ||
      desc.vertices.size() > maximum_vertices || desc.indices.size() > maximum_indices ||
      desc.commands.size() > maximum_commands) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (const auto& vertex : desc.vertices) {
    if (!is_finite(vertex.position[0]) || !is_finite(vertex.position[1]) ||
        !is_finite(vertex.uv[0]) || !is_finite(vertex.uv[1])) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  for (const auto& command : desc.commands) {
    if (command.reserved != 0U || command.texture == GNEISS_NULL_TEXTURE ||
        resources.get_texture(command.texture) == nullptr || !is_finite(command.clip_min[0]) ||
        !is_finite(command.clip_min[1]) || !is_finite(command.clip_max[0]) ||
        !is_finite(command.clip_max[1]) || command.clip_min[0] >= command.clip_max[0] ||
        command.clip_min[1] >= command.clip_max[1] || command.first_index > desc.indices.size() ||
        command.index_count > desc.indices.size() - command.first_index ||
        command.index_count % 3U != 0U) {
      return command.texture != GNEISS_NULL_TEXTURE &&
                     resources.get_texture(command.texture) == nullptr
                 ? GNEISS_ERROR_INVALID_HANDLE
                 : GNEISS_ERROR_INVALID_ARGUMENT;
    }
    for (std::uint32_t item = 0; item < command.index_count; ++item) {
      const auto source_index = desc.indices[command.first_index + item];
      if (source_index > std::numeric_limits<std::uint32_t>::max() - command.vertex_offset ||
          source_index + command.vertex_offset >= desc.vertices.size()) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
    }
  }
  try {
    std::vector<gneiss_ui_vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<gneiss_ui_draw_command> commands;
    if (!desc.vertices.empty()) {
      vertices.assign(desc.vertices.begin(), desc.vertices.end());
    }
    if (!desc.indices.empty()) {
      indices.assign(desc.indices.begin(), desc.indices.end());
    }
    if (!desc.commands.empty()) {
      commands.assign(desc.commands.begin(), desc.commands.end());
    }
    display_width_ = desc.display_width;
    display_height_ = desc.display_height;
    framebuffer_scale_x_ = desc.framebuffer_scale_x;
    framebuffer_scale_y_ = desc.framebuffer_scale_y;
    vertices_ = std::move(vertices);
    indices_ = std::move(indices);
    commands_ = std::move(commands);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

void ui_draw_list::clear() noexcept {
  display_width_ = 0.0F;
  display_height_ = 0.0F;
  framebuffer_scale_x_ = 1.0F;
  framebuffer_scale_y_ = 1.0F;
  vertices_.clear();
  indices_.clear();
  commands_.clear();
}

} // namespace gneiss::render_internal
