// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_BACKEND_GRANIT_GRANIT_RENDER_SERVICE_HPP_
#define GNEISS_RENDER_BACKEND_GRANIT_GRANIT_RENDER_SERVICE_HPP_

#include "engine/platform/native_window_info.hpp"
#include "engine/function/render/backend/granit/pbr_shader_resolver.hpp"
#include "engine/function/render/backend/granit/scene_projection_math.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/render_frame_packet.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/ui_draw_list.hpp"

#include <gneiss/core/result.h>

#include <granit/granit.hpp>
#include <granit/pipeline/canvas_draw_list.hpp>
#include <granit/pipeline/debug_draw_list.hpp>
#include <granit/pipeline/environment_map.hpp>
#include <granit/pipeline/material.hpp>
#include <granit/pipeline/mesh.hpp>
#include <granit/pipeline/render_pipeline.hpp>
#include <granit/pipeline/scene.hpp>
#include <granit/renderer/shader_library.hpp>

#include <array>
#include <atomic>
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

namespace gneiss::log_internal {
class log_dispatcher;
}

namespace gneiss::render_internal {

class granit_render_service final {
public:
  [[nodiscard]] gneiss_result initialize(const application_internal::native_window_info& window,
                                         std::span<const std::byte> environment_asset = {},
                                         float environment_intensity = 1.0F,
                                         float environment_rotation_radians = 0.0F,
                                         log_internal::log_dispatcher* log = nullptr) noexcept;
  [[nodiscard]] gneiss_result shutdown(granit::renderer_resource_stats& stats) noexcept;
  [[nodiscard]] gneiss_result finish_frames() noexcept;
  void set_log_application(gneiss_application application) noexcept {
    log_application_.store(application, std::memory_order_relaxed);
  }
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
  [[nodiscard]] double latest_texture_upload_ms() const noexcept {
    return latest_texture_upload_ms_;
  }
  /** 仅在 initialize 完成后由宿主读取；设备重建必须重新发布。 */
  [[nodiscard]] render_internal::texture_prepare_profile texture_profile() const noexcept {
    return texture_profile_;
  }
  using texture_data = std::shared_ptr<const render_internal::texture_resource>;
  [[nodiscard]] static std::size_t
  estimate_upload_bytes(const render_internal::render_upload_item& item) noexcept;
  [[nodiscard]] gneiss_result
  prepare_textures(std::vector<render_internal::render_upload_item> data,
                   std::uint64_t& sequence) noexcept;
  [[nodiscard]] bool poll_texture_preparation(std::uint64_t sequence,
                                              gneiss_result& result) noexcept;
  [[nodiscard]] gneiss_result
  discard_prepared_textures(std::vector<render_internal::render_upload_item> data,
                            std::uint64_t& sequence) noexcept;

private:
  double latest_texture_upload_ms_{};
  [[nodiscard]] gneiss_result initialize_gpu(const application_internal::native_window_info& window,
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
  struct prepared_texture {
    texture_data data;
    texture_mirror mirror;
  };
  std::unordered_map<const render_internal::texture_resource*, prepared_texture> prepared_textures_;

  struct mesh_mirror final {
    const render_internal::mesh_resource* source{};
    granit::buffer vertices;
    granit::buffer indices;
    granit::mesh mesh;
    std::uint32_t first_index{};
    std::int32_t vertex_offset{};
    std::uint32_t index_count{};
  };

  struct material_mirror final {
    const render_internal::material_resource* source{};
    std::array<granit::sampler, 5> samplers;
    granit::material_instance material;
  };

  struct prepared_mesh {
    std::shared_ptr<const render_internal::mesh_resource> data;
    mesh_mirror mirror;
  };
  struct prepared_material {
    std::shared_ptr<const render_internal::material_resource> data;
    material_mirror mirror;
    std::array<texture_data, 5> dependencies;
  };
  std::unordered_map<const render_internal::mesh_resource*, prepared_mesh> prepared_meshes_;
  std::unordered_map<const render_internal::material_resource*, prepared_material>
      prepared_materials_;
  void discard_candidates(std::span<const render_internal::render_upload_item> data) noexcept;
  [[nodiscard]] granit::result create_mesh_mirror(const render_internal::mesh_resource& source,
                                                  mesh_mirror& output) noexcept;

  [[nodiscard]] granit::result initialize_pipeline() noexcept;
  [[nodiscard]] granit::result
  create_texture_mirror(const render_internal::texture_resource& source, texture_mirror& output,
                        gneiss_texture rid = GNEISS_NULL_TEXTURE) noexcept;
  void log_texture(gneiss_texture rid, const char* stage, granit::result result,
                   std::uint32_t variant, granit::texture_format format, std::uint32_t mips,
                   std::uint64_t bytes) noexcept;
  [[nodiscard]] granit::result
  create_material_mirror(const render_internal::material_resource& source,
                         const std::array<granit_texture_view, 5>& textures,
                         material_mirror& output) noexcept;
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
  render_internal::texture_prepare_profile texture_profile_{};
  granit::surface surface_;
  granit::swapchain swapchain_;
  granit::render_pipeline pipeline_;
  granit::environment_map environment_;
  granit::environment_map_info environment_info_{};
  pbr_shader_resolver pbr_assets_;
  granit::shader_library pbr_library_;
  granit::sampler ui_sampler_;
  granit::canvas_draw_list ui_canvas_;
  granit::debug_draw_list debug_draw_;
  texture_mirror default_white_srgb_;
  texture_mirror default_white_linear_;
  texture_mirror default_normal_linear_;
  std::unordered_map<gneiss_texture, texture_mirror> texture_mirrors_;
  std::unordered_map<gneiss_mesh, mesh_mirror> mesh_mirrors_;
  std::unordered_map<gneiss_material, material_mirror> material_mirrors_;
  granit::texture_format swapchain_format_{granit::texture_format::undefined};
  std::uint64_t last_pipeline_metric_sequence_{};
  bool gpu_timing_supported_{};
  bool environment_asset_requested_{};
  bool environment_fallback_{};
  float environment_intensity_{1.0F};
  float environment_rotation_radians_{};
  // 借用 Application 的线程安全日志队列；其生命周期覆盖执行器停机。
  log_internal::log_dispatcher* log_{};
  std::atomic<gneiss_application> log_application_{GNEISS_NULL_APPLICATION};
  render_internal::threaded_render_executor executor_;
  std::vector<render_internal::render_frame_packet> recycled_frame_packets_;
  std::deque<render_internal::render_frame_completion> required_frame_completions_;
  bool pending_recreate_{};
};

} // namespace gneiss::render_internal

#endif
