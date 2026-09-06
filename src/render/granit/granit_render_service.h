// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_
#define GNEISS_RENDER_GRANIT_GRANIT_RENDER_SERVICE_H_

#include "platform/granit/granit_platform.h"
#include "render/debug_draw_list.h"
#include "render/granit/scene_projection_math.h"
#include "render/granit/pbr_shader_resolver.h"
#include "render/render_executor.h"
#include "render/render_frame_packet.h"
#include "render/render_resource_service.h"
#include "render/ui_draw_list.h"
#include "world/render_snapshot.h"

#include <gneiss/core/result.h>

#include <granit/granit.hpp>
#include <granit/pipeline/canvas_draw_list.hpp>
#include <granit/pipeline/debug_draw_list.hpp>
#include <granit/pipeline/environment_map.hpp>
#include <granit/pipeline/material.hpp>
#include <granit/pipeline/mesh.hpp>
#include <granit/pipeline/render_pipeline.hpp>
#include <granit/pipeline/scene.hpp>

#include <array>
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

namespace gneiss::application_internal {

class granit_render_service final {
public:
  [[nodiscard]] gneiss_result initialize(const native_window_info& window,
                                         std::span<const std::byte> environment_asset = {},
                                         float environment_intensity = 1.0F,
                                         float environment_rotation_radians = 0.0F) noexcept;
  [[nodiscard]] gneiss_result shutdown(granit::renderer_resource_stats& stats) noexcept;
  [[nodiscard]] gneiss_result
  prepare_frame_packet_storage(render_internal::render_frame_packet& packet,
                               bool& out_should_prepare) noexcept;
  [[nodiscard]] gneiss_result submit(render_internal::render_frame_packet packet,
                                     render_internal::render_frame_policy policy =
                                         render_internal::render_frame_policy::replaceable,
                                     std::uint64_t* out_sequence = nullptr) noexcept;
  [[nodiscard]] bool
  try_take_required_completion(render_internal::render_frame_completion& completion) noexcept;
  [[nodiscard]] render_internal::render_queue_stats query_performance_stats() const noexcept;

private:
  [[nodiscard]] gneiss_result initialize_gpu(const native_window_info& window,
                                             std::span<const std::byte> environment_asset,
                                             float environment_intensity,
                                             float environment_rotation_radians) noexcept;
  [[nodiscard]] gneiss_result shutdown_gpu(granit::renderer_resource_stats& stats) noexcept;
  [[nodiscard]] gneiss_result
  execute_frame(render_internal::render_frame_packet& packet,
                render_internal::render_execution_result& output) noexcept;
  [[nodiscard]] gneiss_result collect_completions() noexcept;

  struct texture_mirror final {
    const render_internal::texture_resource* source{};
    granit::texture texture;
    granit::texture_view view;
  };

  struct mesh_mirror final {
    const render_internal::mesh_resource* source{};
    granit::mesh mesh;
    std::uint32_t first_index{};
    std::int32_t vertex_offset{};
    std::uint32_t index_count{};
  };

  struct material_mirror final {
    const render_internal::material_resource* source{};
    gneiss_texture base_color_texture{GNEISS_NULL_TEXTURE};
    granit::material_instance material;
  };

  [[nodiscard]] granit::result initialize_pipeline() noexcept;
  [[nodiscard]] granit::result
  create_texture_mirror(const render_internal::texture_resource& source,
                        texture_mirror& output) noexcept;
  [[nodiscard]] granit::result
  create_material_mirror(const render_internal::material_resource& source,
                         granit_texture_view base_color, material_mirror& output) noexcept;
  [[nodiscard]] granit::result
  rebuild_geometry_arena(const render_internal::render_resource_snapshot& resources) noexcept;
  [[nodiscard]] granit::result ensure_default_textures() noexcept;
  [[nodiscard]] granit::result
  prepare_ui_draw_list(const render_internal::ui_draw_list& ui,
                       const render_internal::render_resource_snapshot& resources,
                       std::uint32_t width, std::uint32_t height) noexcept;
  void
  release_invalid_textures(const render_internal::render_resource_snapshot& resources) noexcept;
  void release_invalid_meshes(const render_internal::render_resource_snapshot& resources) noexcept;
  void
  release_invalid_materials(const render_internal::render_resource_snapshot& resources) noexcept;

  granit::renderer renderer_;
  granit::surface surface_;
  granit::swapchain swapchain_;
  granit::render_pipeline pipeline_;
  granit::environment_map environment_;
  granit_environment_map_info environment_info_{};
  pbr_shader_resolver pbr_assets_;
  granit::sampler sampler_;
  granit::sampler ui_sampler_;
  granit::canvas_draw_list ui_canvas_;
  granit::debug_draw_list debug_draw_;
  texture_mirror default_white_srgb_;
  texture_mirror default_white_linear_;
  texture_mirror default_normal_linear_;
  std::unordered_map<gneiss_texture, texture_mirror> texture_mirrors_;
  std::unordered_map<gneiss_mesh, mesh_mirror> mesh_mirrors_;
  std::unordered_map<gneiss_material, material_mirror> material_mirrors_;
  granit::buffer geometry_vertices_;
  granit::buffer geometry_indices_;
  bool geometry_dirty_{};
  granit::texture_format swapchain_format_{granit::texture_format::undefined};
  std::uint64_t last_pipeline_metric_sequence_{};
  std::deque<std::uint64_t> pending_metric_sequences_;
  bool gpu_timing_supported_{};
  bool environment_asset_requested_{};
  bool environment_fallback_{};
  float environment_intensity_{1.0F};
  float environment_rotation_radians_{};
  render_internal::threaded_render_executor executor_;
  std::vector<render_internal::render_frame_packet> recycled_frame_packets_;
  std::deque<render_internal::render_frame_completion> required_frame_completions_;
  bool pending_recreate_{};
};

} // namespace gneiss::application_internal

#endif
