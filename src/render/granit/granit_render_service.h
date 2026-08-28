// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_
#define GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_

#include "platform/granit/granit_platform.h"
#include "render/granit/object_uniform.h"
#include "render/render_resource_service.h"
#include "render/ui_draw_list.h"
#include "world/render_snapshot.h"

#include <gneiss/core/result.h>

#include <granit/granit.hpp>
#include <granit/pipeline/canvas_draw_list.hpp>

#include <array>
#include <span>
#include <unordered_map>
#include <vector>

namespace gneiss::application_internal {

class granit_render_service final {
public:
  [[nodiscard]] gneiss_result initialize(const native_window_info& window) noexcept;
  [[nodiscard]] gneiss_result render(native_window_info& window,
                                     const world_internal::render_snapshot& snapshot,
                                     const render_internal::render_resource_service& resources,
                                     const render_internal::ui_draw_list& ui) noexcept;

private:
  struct texture_mirror final {
    granit::texture texture;
    granit::texture_view view;
    granit::bind_group group;
  };

  struct mesh_mirror final {
    std::uint32_t first_index{};
    std::int32_t vertex_offset{};
    std::uint32_t index_count{};
  };

  struct uniform_frame final {
    granit::buffer buffer;
    granit::bind_group group;
    std::uint64_t capacity{};
  };

  [[nodiscard]] granit::result initialize_pipeline(granit::texture_format format) noexcept;
  [[nodiscard]] granit::result ensure_depth_target(std::uint32_t width,
                                                   std::uint32_t height) noexcept;
  [[nodiscard]] granit::result
  create_texture_mirror(const render_internal::texture_resource& source,
                        texture_mirror& output) noexcept;
  [[nodiscard]] granit::result
  rebuild_geometry_arena(const render_internal::render_resource_service& resources) noexcept;
  [[nodiscard]] granit::result ensure_default_texture() noexcept;
  [[nodiscard]] granit::result ensure_uniform_arena(uniform_frame& frame,
                                                    std::span<const std::byte> data) noexcept;
  [[nodiscard]] granit::result
  prepare_ui_draw_list(const render_internal::ui_draw_list& ui,
                       const render_internal::render_resource_service& resources,
                       std::uint32_t width, std::uint32_t height) noexcept;
  void release_invalid_textures(const render_internal::render_resource_service& resources) noexcept;
  void release_invalid_meshes(const render_internal::render_resource_service& resources) noexcept;

  granit::renderer renderer_;
  granit::surface surface_;
  granit::swapchain swapchain_;
  granit::frame_context frame_context_;
  granit::shader vertex_shader_;
  granit::shader fragment_shader_;
  granit::bind_group_layout texture_layout_;
  granit::bind_group_layout object_layout_;
  granit::pipeline_layout pipeline_layout_;
  granit::graphics_pipeline pipeline_;
  granit::texture depth_texture_;
  granit::texture_view depth_view_;
  granit::sampler sampler_;
  granit::sampler ui_sampler_;
  granit::canvas_draw_list ui_canvas_;
  texture_mirror default_texture_;
  std::unordered_map<gneiss_texture, texture_mirror> texture_mirrors_;
  std::unordered_map<gneiss_mesh, mesh_mirror> mesh_mirrors_;
  granit::buffer geometry_vertices_;
  granit::buffer geometry_indices_;
  bool geometry_dirty_{};
  granit::texture_format swapchain_format_{granit::texture_format::undefined};
  std::uint32_t depth_width_{};
  std::uint32_t depth_height_{};
  std::array<uniform_frame, 3> uniform_frames_;
  std::uint64_t uniform_stride_{};
  std::uint64_t frame_index_{};
};

} // namespace gneiss::application_internal

#endif
