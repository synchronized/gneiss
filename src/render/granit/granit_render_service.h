// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_
#define GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_

#include "platform/granit/granit_platform.h"
#include "render/render_resource_service.h"
#include "world/render_snapshot.h"

#include <gneiss/core/result.h>

#include <granit/granit.hpp>

#include <array>
#include <unordered_map>
#include <vector>

namespace gneiss::application_internal {

class granit_render_service final {
public:
  [[nodiscard]] gneiss_result initialize(const native_window_info& window) noexcept;
  [[nodiscard]] gneiss_result
  render(native_window_info& window, const world_internal::render_snapshot& snapshot,
         const render_internal::render_resource_service& resources) noexcept;

private:
  struct texture_mirror final {
    granit::texture texture;
    granit::texture_view view;
    granit::bind_group group;
  };

  [[nodiscard]] granit::result initialize_pipeline(granit::texture_format format) noexcept;
  [[nodiscard]] granit::result ensure_depth_target(std::uint32_t width,
                                                   std::uint32_t height) noexcept;
  [[nodiscard]] granit::result
  create_texture_mirror(const render_internal::texture_resource& source,
                        texture_mirror& output) noexcept;
  [[nodiscard]] granit::result ensure_default_texture() noexcept;
  void release_invalid_textures(const render_internal::render_resource_service& resources) noexcept;

  granit::renderer renderer_;
  granit::surface surface_;
  granit::swapchain swapchain_;
  granit::frame_context frame_context_;
  granit::shader vertex_shader_;
  granit::shader fragment_shader_;
  granit::bind_group_layout texture_layout_;
  granit::pipeline_layout pipeline_layout_;
  granit::graphics_pipeline pipeline_;
  granit::texture depth_texture_;
  granit::texture_view depth_view_;
  granit::sampler sampler_;
  texture_mirror default_texture_;
  std::unordered_map<gneiss_texture, texture_mirror> texture_mirrors_;
  granit::texture_format swapchain_format_{granit::texture_format::undefined};
  std::uint32_t depth_width_{};
  std::uint32_t depth_height_{};
  std::array<std::vector<granit::buffer>, 3> frame_vertex_buffers_;
  std::uint64_t frame_index_{};
};

} // namespace gneiss::application_internal

#endif
