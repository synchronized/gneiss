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
#include <vector>

namespace gneiss::application_internal {

class granit_render_service final {
public:
  [[nodiscard]] gneiss_result initialize(const native_window_info& window) noexcept;
  [[nodiscard]] gneiss_result
  render(native_window_info& window, const world_internal::render_snapshot& snapshot,
         const render_internal::render_resource_service& resources) noexcept;

private:
  [[nodiscard]] granit::result initialize_pipeline(granit::texture_format format) noexcept;

  granit::renderer renderer_;
  granit::surface surface_;
  granit::swapchain swapchain_;
  granit::frame_context frame_context_;
  granit::shader vertex_shader_;
  granit::shader fragment_shader_;
  granit::pipeline_layout pipeline_layout_;
  granit::graphics_pipeline pipeline_;
  granit::texture_format swapchain_format_{granit::texture_format::undefined};
  std::array<std::vector<granit::buffer>, 3> frame_vertex_buffers_;
  std::uint64_t frame_index_{};
};

} // namespace gneiss::application_internal

#endif
