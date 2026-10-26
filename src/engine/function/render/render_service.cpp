// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_service.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/render/backend/granit/pbr_shader_resolver.hpp"
#include "engine/function/render/debug_draw_list.hpp"
#include "engine/function/render/render_asset_loader.hpp"
#include "engine/function/render/render_executor.hpp"
#include "engine/function/render/render_frame_packet.hpp"
#include "engine/function/render/render_resource_service.hpp"
#include "engine/function/render/ui_draw_list.hpp"
#include "engine/platform/native_window_info.hpp"
#include <array>
#include <atomic>
#include <deque>
#include <granit/granit.hpp>
#include <granit/pipeline/canvas_draw_list.hpp>
#include <granit/pipeline/debug_draw_list.hpp>
#include <granit/pipeline/environment_map.hpp>
#include <granit/pipeline/material.hpp>
#include <granit/pipeline/mesh.hpp>
#include <granit/pipeline/render_pipeline.hpp>
#include <granit/pipeline/scene.hpp>
#include <granit/renderer/shader_library.hpp>
#include <span>
#include <unordered_map>
#include <vector>

#include "engine/core/log/log_dispatcher.hpp"
#include "engine/resource/loaders/texture_container.hpp"

#include <granit/core/version.h>
#include <granit/renderer/native_surface.hpp>
#include <granit/renderer/readback_batch.hpp>
#include <granit/renderer/texture_asset.hpp>
#include <granit/renderer/upload_batch.hpp>
#include <thread>

#include <granit/pipeline/pbr_material.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

namespace gneiss::engine {
struct render_service::implementation final {
public:
  render_resource_service resources_;
  [[nodiscard]] gneiss::engine::result
  initialize(const gneiss::engine::native_window_info& window,
             std::span<const std::byte> environment_asset = {}, float environment_intensity = 1.0F,
             float environment_rotation_radians = 0.0F,
             gneiss::engine::log_dispatcher* log = nullptr) noexcept;
  [[nodiscard]] gneiss::engine::result shutdown(granit::renderer_resource_stats& stats) noexcept;
  [[nodiscard]] gneiss::engine::result finish_frames() noexcept;
  void set_completion_notification(
      std::shared_ptr<gneiss::engine::progress_notification> notification) noexcept {
    executor_.set_completion_notification(std::move(notification));
  }
  [[nodiscard]] gneiss::engine::result
  prepare_frame_packet_storage(gneiss::engine::render_frame_packet& packet,
                               bool& out_should_prepare) noexcept;
  [[nodiscard]] gneiss::engine::result submit(
      gneiss::engine::render_frame_packet packet,
      gneiss::engine::render_frame_policy policy = gneiss::engine::render_frame_policy::replaceable,
      std::uint64_t* out_sequence = nullptr) noexcept;
  [[nodiscard]] bool
  try_take_required_completion(gneiss::engine::render_frame_completion& completion) noexcept;
  [[nodiscard]] gneiss::engine::render_queue_stats query_performance_stats() const noexcept;
  /** 仅在 initialize 完成后由宿主读取；设备重建必须重新发布。 */
  [[nodiscard]] gneiss::engine::texture_prepare_profile texture_profile() const noexcept {
    return texture_profile_;
  }

private:
  [[nodiscard]] gneiss::engine::result
  initialize_gpu(const gneiss::engine::native_window_info& window,
                 std::span<const std::byte> environment_asset, float environment_intensity,
                 float environment_rotation_radians) noexcept;
  [[nodiscard]] gneiss::engine::result
  shutdown_gpu(granit::renderer_resource_stats& stats) noexcept;
  [[nodiscard]] gneiss::engine::result
  execute_frame(gneiss::engine::render_frame_packet& packet,
                gneiss::engine::render_execution_result& output) noexcept;
  [[nodiscard]] gneiss::engine::result collect_completions() noexcept;

  struct texture_mirror final {
    const gneiss::engine::texture_data* source{};
    std::shared_ptr<const gneiss::engine::texture_data> data;
    granit::texture texture;
    granit::texture_view view;
    // 异步完成时输出实际选择的上传信息，不能在提交阶段报告 ready。
    granit::texture_format format{};
    std::uint32_t variant{};
    std::uint32_t mips{};
    std::uint64_t bytes{};
  };
  struct pending_texture {
    std::shared_ptr<texture_mirror> mirror;
    granit::async_operation operation;
  };
  std::unordered_map<rid, pending_texture> pending_textures_;
  void refresh_texture(rid handle, std::shared_ptr<const gneiss::engine::texture_data> data);
  struct mesh_mirror final {
    const gneiss::engine::mesh_data* source{};
    std::shared_ptr<const gneiss::engine::mesh_data> data;
    granit::buffer vertices;
    granit::buffer indices;
    granit::mesh mesh;
    std::uint32_t first_index{};
    std::int32_t vertex_offset{};
    std::uint32_t index_count{};
  };

  struct material_mirror final {
    const gneiss::engine::material_data* source{};
    std::shared_ptr<const gneiss::engine::material_data> data;
    std::array<std::shared_ptr<const texture_mirror>, 5> dependencies;
    std::array<granit_texture_view, 5> textures{};
    std::array<granit::sampler, 5> samplers;
    granit::material_instance material;
  };

  [[nodiscard]] granit::result create_mesh_mirror(const gneiss::engine::mesh_data& source,
                                                  mesh_mirror& output) noexcept;

  [[nodiscard]] granit::result initialize_pipeline() noexcept;
  [[nodiscard]] granit::result
  create_texture_mirror(const gneiss::engine::texture_data& source, texture_mirror& output,
                        gneiss::engine::rid rid = gneiss::engine::null_rid,
                        granit::async_operation* operation = nullptr) noexcept;
  void log_texture(gneiss::engine::rid rid, const char* stage, granit::result result,
                   std::uint32_t variant, granit::texture_format format, std::uint32_t mips,
                   std::uint64_t bytes) noexcept;
  [[nodiscard]] granit::result
  create_material_mirror(const gneiss::engine::material_data& source,
                         const std::array<granit_texture_view, 5>& textures,
                         material_mirror& output) noexcept;
  [[nodiscard]] granit::result ensure_default_textures() noexcept;
  [[nodiscard]] granit::result
  prepare_ui_draw_list(const gneiss::engine::ui_draw_list& ui,
                       const gneiss::engine::render_resource_snapshot& resources,
                       std::uint32_t width, std::uint32_t height) noexcept;
  void release_invalid_textures(const gneiss::engine::render_resource_snapshot& resources) noexcept;
  void release_invalid_meshes(const gneiss::engine::render_resource_snapshot& resources) noexcept;
  void
  release_invalid_materials(const gneiss::engine::render_resource_snapshot& resources) noexcept;

  granit::renderer renderer_;
  gneiss::engine::texture_prepare_profile texture_profile_{};
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
  std::unordered_map<gneiss::engine::rid, std::shared_ptr<texture_mirror>> texture_mirrors_;
  std::unordered_map<gneiss::engine::rid, mesh_mirror> mesh_mirrors_;
  std::unordered_map<gneiss::engine::rid, material_mirror> material_mirrors_;
  granit::texture_format swapchain_format_{granit::texture_format::undefined};
  std::uint64_t last_pipeline_metric_sequence_{};
  bool gpu_timing_supported_{};
  bool environment_asset_requested_{};
  bool environment_fallback_{};
  float environment_intensity_{1.0F};
  float environment_rotation_radians_{};
  // 借用 Application 的线程安全日志队列；其生命周期覆盖执行器停机。
  gneiss::engine::log_dispatcher* log_{};
  gneiss::engine::threaded_render_executor executor_;
  std::vector<gneiss::engine::render_frame_packet> recycled_frame_packets_;
  std::deque<gneiss::engine::render_frame_completion> required_frame_completions_;
  bool pending_recreate_{};
};
namespace {

static_assert(GRANIT_VERSION_MAJOR > 0 || GRANIT_VERSION_MINOR >= 44,
              "Gneiss requires Granit 0.44.0 or newer");

gneiss::engine::result map_result(granit::result result) noexcept {
  switch (result.native()) {
  case granit::result::success.native():
    return gneiss::engine::result::success;
  case granit::result::invalid_argument.native():
    return gneiss::engine::result::invalid_argument;
  case granit::result::invalid_handle.native():
    return gneiss::engine::result::invalid_handle;
  case granit::result::out_of_memory.native():
    return gneiss::engine::result::out_of_memory;
  case granit::result::unsupported.native():
  case granit::result::backend_unavailable.native():
    return gneiss::engine::result::unsupported;
  case granit::result::not_ready.native():
  case granit::result::out_of_date.native():
    return gneiss::engine::result::not_ready;
  case granit::result::initialization_failed.native():
  case granit::result::incompatible_driver.native():
    return gneiss::engine::result::initialization_failed;
  default:
    return gneiss::engine::result::dependency_failed;
  }
}

struct gpu_vertex {
  std::array<float, 3> position;
  std::array<float, 3> normal;
  std::array<float, 4> tangent;
  std::array<float, 2> texture_coordinate;
  std::array<float, 2> uv1;
  std::array<float, 4> color;
};

struct geometry_range final {
  std::uint32_t first_index{};
  std::int32_t vertex_offset{};
  std::uint32_t index_count{};
};

gneiss::engine::matrix4 multiply(const gneiss::engine::matrix4& left,
                                 const gneiss::engine::matrix4& right) noexcept {
  return left * right;
}

granit_matrix4 to_granit_matrix(const gneiss::engine::matrix4& source) noexcept {
  granit_matrix4 result{};
  std::ranges::copy(gneiss::engine::matrix_elements(source), result.elements);
  return result;
}

float mesh_bounds_radius(const gneiss::engine::mesh_data& mesh,
                         const gneiss::engine::transform& transform) noexcept {
  float radius_squared = 0.0F;
  for (const auto& vertex : mesh.vertices) {
    radius_squared =
        std::max(radius_squared, vertex.x * vertex.x + vertex.y * vertex.y + vertex.z * vertex.z);
  }
  const auto scale = std::max(
      {std::abs(transform.scale[0]), std::abs(transform.scale[1]), std::abs(transform.scale[2])});
  return std::sqrt(radius_squared) * scale;
}

granit::result append_mesh_geometry(const gneiss::engine::mesh_data& source,
                                    std::vector<gpu_vertex>& vertices,
                                    std::vector<std::uint32_t>& indices, geometry_range& range) {
  const auto source_index_count =
      source.indices.empty() ? source.vertices.size() : source.indices.size();
  if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) -
                            source.vertices.size() ||
      indices.size() > std::numeric_limits<std::uint32_t>::max() - source_index_count) {
    return granit::result::out_of_memory;
  }
  range.vertex_offset = static_cast<std::int32_t>(vertices.size());
  range.first_index = static_cast<std::uint32_t>(indices.size());
  range.index_count = static_cast<std::uint32_t>(source_index_count);
  for (std::size_t index = 0; index < source.vertices.size(); ++index) {
    const auto& vertex = source.vertices[index];
    const auto normal = source.normals.empty() ? gneiss::engine::mesh_normal{0.0F, 1.0F, 0.0F}
                                               : source.normals[index];
    const auto tangent = source.tangents.empty()
                             ? gneiss::engine::mesh_tangent{1.0F, 0.0F, 0.0F, 1.0F}
                             : source.tangents[index];
    const auto uv1 = source.uv1.empty() ? gneiss::engine::mesh_uv{0, 0} : source.uv1[index];
    const auto color =
        source.colors.empty() ? gneiss::engine::mesh_color{1, 1, 1, 1} : source.colors[index];
    vertices.push_back({.position = {vertex.x, vertex.y, vertex.z},
                        .normal = {normal.x, normal.y, normal.z},
                        .tangent = {tangent.x, tangent.y, tangent.z, tangent.w},
                        .texture_coordinate = {vertex.u, vertex.v},
                        .uv1 = {uv1.u, uv1.v},
                        .color = {color.r, color.g, color.b, color.a}});
  }
  if (source.indices.empty()) {
    for (std::size_t index = 0; index < source.vertices.size(); ++index) {
      indices.push_back(static_cast<std::uint32_t>(index));
    }
  } else {
    indices.insert(indices.end(), source.indices.begin(), source.indices.end());
  }
  return granit::result::success;
}

} // namespace

granit::result render_service::implementation::initialize_pipeline() noexcept {
  if (const auto result = pbr_assets_.initialize_embedded(); result != GRANIT_SUCCESS)
    return granit::from_native(result);
  if (const auto result = pbr_library_.initialize(renderer_, pbr_assets_.shader_archive());
      result.failed())
    return result;
  granit::render_pipeline_desc desc{};
  auto result = pipeline_.initialize(renderer_, desc);
  if (result.ok()) {
    const auto metrics_result = pipeline_.enable_metrics();
    gpu_timing_supported_ = metrics_result.ok();
    if (metrics_result.failed() && metrics_result != granit::result::unsupported &&
        metrics_result != granit::result::backend_unavailable)
      result = metrics_result;
  }
  return result;
}

void render_service::implementation::log_texture(gneiss::engine::rid rid, const char* stage,
                                                 granit::result result, std::uint32_t variant,
                                                 granit::texture_format format, std::uint32_t mips,
                                                 std::uint64_t bytes) noexcept {
  if (log_ == nullptr) {
    return;
  }
  const char* name = "undefined";
  switch (format) {
  case granit::texture_format::bc7_rgba_srgb:
    name = "BC7_SRGB";
    break;
  case granit::texture_format::bc7_rgba_unorm:
    name = "BC7_UNORM";
    break;
  case granit::texture_format::rgba8_srgb:
    name = "RGBA8_SRGB";
    break;
  case granit::texture_format::rgba8_unorm:
    name = "RGBA8_UNORM";
    break;
  default:
    break;
  }
  std::array<char, 256> buffer{};
  const auto length = std::snprintf(buffer.data(), buffer.size(),
                                    "rid=%llu stage=%s variant=%u format=%s mips=%u bytes=%llu",
                                    static_cast<unsigned long long>(rid), stage, variant, name,
                                    mips, static_cast<unsigned long long>(bytes));
  if (length <= 0) {
    return;
  }
  constexpr std::string_view category = "render.texture";
  static_cast<void>(log_->submit({
      .severity = result.ok() ? gneiss::engine::level::info : gneiss::engine::level::error,
      .source = "granit.render.texture",
      .category = category,
      .message = {buffer.data(), std::min(static_cast<std::size_t>(length), buffer.size() - 1U)},
      .result = gneiss::engine::result{map_result(result)},
  }));
}

granit::result render_service::implementation::create_texture_mirror(
    const gneiss::engine::texture_data& source, texture_mirror& output, gneiss::engine::rid rid,
    granit::async_operation* operation) noexcept {
  if (source.profile.generation != 0U && source.profile != texture_profile_) {
    return granit::result::invalid_argument;
  }

  output.source = &source;
  if (!source.manifest.empty()) {
    granit::texture_asset_info info;
    granit::texture_asset_selection selection;
    auto result = granit::inspect_texture_asset(source.manifest, info);
    const char* stage = "inspect";
    if (result.ok()) {
      stage = "select";
      result = granit::select_texture_asset_variant(
          renderer_, source.manifest, selection,
          {.required_usage =
               granit::texture_usage::sampled | granit::texture_usage::transfer_destination});
    }
    if (result.failed() || selection.variant_index >= info.variants.size()) {
      output.source = nullptr;
      result = result.failed() ? result : granit::result::invalid_argument;
      log_texture(rid, stage, result, selection.variant_index, selection.format, info.mip_levels,
                  0U);
      return result;
    }
    const auto& variant = info.variants[selection.variant_index];
    const bool selected = source.profile.generation != 0U;
    const std::span<const std::byte> available_payload = source.payload;
    if (selected &&
        (source.profile != texture_profile_ || source.selected_variant != selection.variant_index ||
         available_payload.size() != variant.payload_size)) {
      output.source = nullptr;
      return granit::result::invalid_argument;
    }
    const auto payload_offset = selected ? 0U : variant.payload_offset;
    if (payload_offset > available_payload.size() ||
        variant.payload_size > available_payload.size() - payload_offset) {
      output.source = nullptr;
      return granit::result::invalid_argument;
    }
    const auto format = static_cast<granit::texture_format>(selection.format);
    output.format = format;
    output.variant = selection.variant_index;
    output.mips = info.mip_levels;
    output.bytes = variant.payload_size;
    stage = "create";
    result = output.texture.initialize(
        renderer_,
        {.dimension = granit::texture_dimension::two_dimensional,
         .format = format,
         .usage = granit::texture_usage::sampled | granit::texture_usage::transfer_destination,
         .location = granit::memory_location::device,
         .width = info.width,
         .height = info.height,
         .mip_levels = info.mip_levels});
    granit::upload_batch upload;
    if (result.ok()) {
      stage = "batch";
      result = upload.initialize(renderer_, {.max_staged_bytes = variant.payload_size,
                                             .max_operation_count = variant.subresource_count});
    }
    if (result.ok()) {
      stage = "write";
      const auto payload = available_payload.subspan(
          static_cast<std::size_t>(payload_offset), static_cast<std::size_t>(variant.payload_size));
      result = granit::write_texture_asset_variant_mips(
          upload, output.texture.ref(), source.manifest, payload, selection.variant_index, 0U,
          info.mip_levels);
    }
    if (result.ok()) {
      stage = "view";
      result = output.view.initialize(renderer_, output.texture,
                                      {.format = format, .mip_level_count = info.mip_levels});
    }
    if (result.ok()) {
      stage = "submit";
      result = operation ? upload.submit_async(*operation) : upload.submit();
    }
    if (result.failed()) {
      static_cast<void>(output.view.reset());
      static_cast<void>(output.texture.reset());
      output.source = nullptr;
    }
    log_texture(rid, result.ok() ? (operation ? "submitted" : "ready") : stage, result,
                selection.variant_index, format, info.mip_levels, variant.payload_size);
    return result;
  }
  const auto format = source.color_space == gneiss::engine::texture_color_space::srgb
                          ? granit::texture_format::rgba8_srgb
                          : granit::texture_format::rgba8_unorm;
  auto result = output.texture.initialize(
      renderer_,
      {.dimension = granit::texture_dimension::two_dimensional,
       .format = format,
       .usage = granit::texture_usage::sampled | granit::texture_usage::transfer_destination,
       .location = granit::memory_location::device,
       .width = source.width,
       .height = source.height,
       .mip_levels = static_cast<std::uint32_t>(source.levels.size())});
  granit::upload_batch upload;
  std::uint64_t upload_bytes{};
  for (const auto& mip : source.levels) {
    upload_bytes += mip.pixels.size();
  }
  output.format = format;
  output.mips = static_cast<std::uint32_t>(source.levels.size());
  output.bytes = upload_bytes;
  if (result.ok()) {
    result = upload.initialize(
        renderer_, {.max_staged_bytes = upload_bytes,
                    .max_operation_count = static_cast<std::uint32_t>(source.levels.size())});
  }
  for (std::uint32_t level = 0U; result.ok() && level < source.levels.size(); ++level) {
    const auto& mip = source.levels[level];
    result = upload.write_texture(
        output.texture.ref(), mip.pixels,
        {.offset = 0, .bytes_per_row = mip.width * 4U, .rows_per_image = mip.height},
        {.mip_level = level, .width = mip.width, .height = mip.height});
  }
  if (result.ok()) {
    result = output.view.initialize(
        renderer_, output.texture,
        {.format = format, .mip_level_count = static_cast<std::uint32_t>(source.levels.size())});
  }
  if (result.ok()) {
    result = operation ? upload.submit_async(*operation) : upload.submit();
  }
  if (result.failed()) {
    static_cast<void>(output.view.reset());
    static_cast<void>(output.texture.reset());
    output.source = nullptr;
  }
  return result;
}

granit::result render_service::implementation::create_material_mirror(
    const gneiss::engine::material_data& source, const std::array<granit_texture_view, 5>& textures,
    material_mirror& output) noexcept {
  output.textures = textures;
  const auto& color = source.parameters.color;
  constexpr std::array addresses{granit::address_mode::repeat, granit::address_mode::clamp_to_edge,
                                 granit::address_mode::mirrored_repeat};
  for (std::size_t slot = 0; slot < source.parameters.sampling.size(); ++slot) {
    const auto& sampling = source.parameters.sampling[slot];
    const auto initialized = output.samplers[slot].initialize(
        renderer_,
        {.mag_filter = sampling.mag_filter == gneiss::engine::texture_filter::linear
                           ? granit::filter::linear
                           : granit::filter::nearest,
         .min_filter = sampling.min_filter == gneiss::engine::texture_filter::linear
                           ? granit::filter::linear
                           : granit::filter::nearest,
         .mip_filter = sampling.mip_filter == gneiss::engine::texture_mip_filter::linear
                           ? granit::mipmap_filter::linear
                           : granit::mipmap_filter::nearest,
         .address_u = addresses[static_cast<std::size_t>(sampling.address_u)],
         .address_v = addresses[static_cast<std::size_t>(sampling.address_v)],
         .max_lod =
             sampling.mip_filter == gneiss::engine::texture_mip_filter::none ? 0.0F : 14.0F});
    if (initialized.failed())
      return initialized;
  }
  const auto uv1_mask = source.uv1_mask();
  const float normal_scale = source.parameters.normal_scale;
  const float occlusion_strength = source.parameters.occlusion_strength;
  const auto& emissive = source.parameters.emissive;
  constexpr std::uint32_t debug_display = 0;
  const std::array updates{
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_ALPHA_CUTOFF),
          granit::material_parameter_type::float32,
          std::as_bytes(std::span{&source.parameters.alpha_cutoff, 1})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_UV1_MASK),
          granit::material_parameter_type::uint32, std::as_bytes(std::span{&uv1_mask, 1})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_BASE_COLOR),
          granit::material_parameter_type::float4,
          std::as_bytes(std::span{color.data(), color.size()})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_METALLIC),
          granit::material_parameter_type::float32,
          std::as_bytes(std::span{&source.parameters.metallic, 1})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_PERCEPTUAL_ROUGHNESS),
          granit::material_parameter_type::float32,
          std::as_bytes(std::span{&source.parameters.roughness, 1})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_NORMAL_SCALE),
          granit::material_parameter_type::float32, std::as_bytes(std::span{&normal_scale, 1})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_OCCLUSION_STRENGTH),
          granit::material_parameter_type::float32,
          std::as_bytes(std::span{&occlusion_strength, 1})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_EMISSIVE),
          granit::material_parameter_type::float3,
          std::as_bytes(std::span{emissive.data(), emissive.size()})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_DEBUG_DISPLAY),
          granit::material_parameter_type::uint32, std::as_bytes(std::span{&debug_display, 1})),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_BASE_COLOR_TEXTURE),
          granit::texture_view_ref::from_native(textures[0])),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_METALLIC_ROUGHNESS_TEXTURE),
          granit::texture_view_ref::from_native(textures[1])),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_NORMAL_TEXTURE),
          granit::texture_view_ref::from_native(textures[2])),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_OCCLUSION_TEXTURE),
          granit::texture_view_ref::from_native(textures[3])),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_EMISSIVE_TEXTURE),
          granit::texture_view_ref::from_native(textures[4])),
      granit::material_parameter_update::sampler_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_BASE_COLOR_SAMPLER),
          output.samplers[0].ref()),
      granit::material_parameter_update::sampler_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_METALLIC_ROUGHNESS_SAMPLER),
          output.samplers[1].ref()),
      granit::material_parameter_update::sampler_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_NORMAL_SAMPLER),
          output.samplers[2].ref()),
      granit::material_parameter_update::sampler_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_OCCLUSION_SAMPLER),
          output.samplers[3].ref()),
      granit::material_parameter_update::sampler_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_EMISSIVE_SAMPLER),
          output.samplers[4].ref())};
  const granit::material_desc desc{.archive = pbr_shader_resolver::material_archive(),
                                   .initial_updates = updates,
                                   .shader_library = pbr_library_.ref()};
  auto result = output.material.initialize(renderer_, desc);
  if (result.ok()) {
    output.source = &source;
  }
  return result;
}

granit::result
render_service::implementation::create_mesh_mirror(const gneiss::engine::mesh_data& source,
                                                   mesh_mirror& output) noexcept {
  try {

    std::vector<gpu_vertex> vertices;
    std::vector<std::uint32_t> indices;
    geometry_range range;
    auto result = append_mesh_geometry(source, vertices, indices, range);
    if (result.failed()) {
      return result;
    }
    result = output.vertices.initialize(
        renderer_,
        {.size = vertices.size() * sizeof(gpu_vertex),
         .usage = granit::buffer_usage::vertex | granit::buffer_usage::transfer_destination});
    if (result.ok()) {
      result = output.indices.initialize(
          renderer_,
          {.size = indices.size() * sizeof(std::uint32_t),
           .usage = granit::buffer_usage::index | granit::buffer_usage::transfer_destination});
    }
    granit::upload_batch upload;
    if (result.ok()) {
      result =
          upload.initialize(renderer_, {.max_staged_bytes = vertices.size() * sizeof(gpu_vertex) +
                                                            indices.size() * sizeof(std::uint32_t),
                                        .max_operation_count = 2U});
    }
    if (result.ok()) {
      result = upload.write_buffer(output.vertices.ref(), 0U, std::as_bytes(std::span{vertices}));
    }
    if (result.ok()) {
      result = upload.write_buffer(output.indices.ref(), 0U, std::as_bytes(std::span{indices}));
    }
    if (result.ok()) {
      result = upload.submit();
    }
    if (result.failed()) {
      return result;
    }
    const std::array attributes{
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_POSITION, GRANIT_VERTEX_FORMAT_FLOAT32X3,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, position)), 0},
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_NORMAL, GRANIT_VERTEX_FORMAT_FLOAT32X3,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, normal)), 0},
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_TANGENT, GRANIT_VERTEX_FORMAT_FLOAT32X4,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, tangent)), 0},
        granit_vertex_attribute{
            GRANIT_PBR_VERTEX_LOCATION_UV0, GRANIT_VERTEX_FORMAT_FLOAT32X2,
            static_cast<std::uint32_t>(offsetof(gpu_vertex, texture_coordinate)), 0},
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_UV1, GRANIT_VERTEX_FORMAT_FLOAT32X2,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, uv1)), 0},
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_COLOR, GRANIT_VERTEX_FORMAT_FLOAT32X4,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, color)), 0}};
    const granit_vertex_buffer_layout layout{sizeof(gpu_vertex), GRANIT_VERTEX_STEP_MODE_VERTEX,
                                             static_cast<std::uint32_t>(attributes.size()), 0,
                                             attributes.data()};
    if (granit_pbr_validate_vertex_layout(&layout, 1, GRANIT_PBR_TEXTURE_ALL, 0U, 0U) !=
        GRANIT_PBR_VERTEX_LAYOUT_VALID) {
      return granit::result::invalid_argument;
    }
    std::array<granit::vertex_attribute, 6> typed_attributes;
    for (std::size_t index = 0; index < attributes.size(); ++index) {
      typed_attributes[index] = {attributes[index].location,
                                 static_cast<granit::vertex_format>(attributes[index].format),
                                 attributes[index].offset};
    }
    const granit::vertex_buffer_layout typed_layout{
        sizeof(gpu_vertex), granit::vertex_step_mode::vertex, typed_attributes};
    const granit::mesh_vertex_buffer vertex_buffer{output.vertices.ref(), 0, typed_layout};
    granit::mesh_desc desc{};
    desc.vertex_buffers = {&vertex_buffer, 1};
    desc.index_buffer = output.indices.ref();
    desc.index_format = granit::index_type::uint32;
    desc.index_count = range.index_count;
    result = output.mesh.initialize(renderer_, desc);
    if (result.ok()) {
      output.source = &source;
      output.index_count = range.index_count;
      output.first_index = 0U;
      output.vertex_offset = 0;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return granit::result::out_of_memory;
  } catch (...) {
    return granit::result::unknown;
  }
}

granit::result render_service::implementation::ensure_default_textures() noexcept {
  if (default_white_srgb_.view.valid() && default_white_linear_.view.valid() &&
      default_normal_linear_.view.valid()) {
    return granit::result::success;
  }
  try {
    gneiss::engine::texture_data white_srgb{
        .width = 1,
        .height = 1,
        .format = gneiss::engine::texture_format::rgba8_unorm,
        .color_space = gneiss::engine::texture_color_space::srgb,
        .levels = {{.width = 1U,
                    .height = 1U,
                    .pixels = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
                               std::byte{0xff}}}},
        .manifest = {},
        .payload = {}};
    gneiss::engine::texture_data white_linear = white_srgb;
    white_linear.color_space = gneiss::engine::texture_color_space::linear;
    gneiss::engine::texture_data normal_linear = white_linear;
    normal_linear.levels.front().pixels = {std::byte{0x80}, std::byte{0x80}, std::byte{0xff},
                                           std::byte{0xff}};
    auto result = create_texture_mirror(white_srgb, default_white_srgb_);
    if (result.ok())
      result = create_texture_mirror(white_linear, default_white_linear_);
    if (result.ok())
      result = create_texture_mirror(normal_linear, default_normal_linear_);
    return result;
  } catch (const std::bad_alloc&) {
    return granit::result::out_of_memory;
  } catch (...) {
    return granit::result::unknown;
  }
}

granit::result render_service::implementation::prepare_ui_draw_list(
    const gneiss::engine::ui_draw_list& ui,
    const gneiss::engine::render_resource_snapshot& resources, std::uint32_t width,
    std::uint32_t height) noexcept {
  auto result = ui_canvas_.clear();
  if (result.failed() || ui.commands().empty()) {
    return result;
  }
  try {
    std::vector<granit_canvas_vertex> vertices;
    vertices.reserve(ui.vertices().size());
    for (const auto& vertex : ui.vertices()) {
      vertices.push_back({vertex.position[0] * ui.framebuffer_scale_x(),
                          vertex.position[1] * ui.framebuffer_scale_y(), vertex.uv[0], vertex.uv[1],
                          vertex.color_rgba8});
    }

    std::vector<std::uint32_t> indices;
    std::vector<granit::canvas_draw_range> ranges;
    indices.reserve(ui.indices().size());
    ranges.reserve(ui.commands().size());
    for (const auto& command : ui.commands()) {
      const auto left = std::clamp(std::floor(command.clip_min[0] * ui.framebuffer_scale_x()), 0.0F,
                                   static_cast<float>(width));
      const auto top = std::clamp(std::floor(command.clip_min[1] * ui.framebuffer_scale_y()), 0.0F,
                                  static_cast<float>(height));
      const auto right = std::clamp(std::ceil(command.clip_max[0] * ui.framebuffer_scale_x()), 0.0F,
                                    static_cast<float>(width));
      const auto bottom = std::clamp(std::ceil(command.clip_max[1] * ui.framebuffer_scale_y()),
                                     0.0F, static_cast<float>(height));
      if (left >= right || top >= bottom || command.index_count == 0U) {
        continue;
      }
      refresh_texture(resource_handle(command.texture),
                      resources.share_texture(resource_handle(command.texture)));
      const auto found = texture_mirrors_.find(resource_handle(command.texture));
      if (found == texture_mirrors_.end())
        continue;
      const auto first_index = static_cast<std::uint32_t>(indices.size());
      for (std::uint32_t item = 0; item < command.index_count; ++item) {
        indices.push_back(ui.indices()[command.first_index + item] + command.vertex_offset);
      }
      ranges.push_back({.first_index = first_index,
                        .index_count = command.index_count,
                        .state = {.texture = found->second->view.ref(),
                                  .sampler = ui_sampler_.ref(),
                                  .clip = {.x = static_cast<std::int32_t>(left),
                                           .y = static_cast<std::int32_t>(top),
                                           .width = static_cast<std::uint32_t>(right - left),
                                           .height = static_cast<std::uint32_t>(bottom - top)}}});
    }
    return ranges.empty() ? granit::result::success
                          : ui_canvas_.append_batch(vertices, indices, ranges);
  } catch (const std::bad_alloc&) {
    return granit::result::out_of_memory;
  } catch (...) {
    return granit::result::unknown;
  }
}

void render_service::implementation::refresh_texture(
    rid handle, std::shared_ptr<const gneiss::engine::texture_data> data) {
  if (!data)
    return;
  auto active = texture_mirrors_.find(handle);
  if (active != texture_mirrors_.end() && active->second->data == data)
    return;
  auto pending = pending_textures_.find(handle);
  if (pending != pending_textures_.end()) {
    granit::async_operation_status status;
    auto result = pending->second.operation.get_status(status);
    if (result.ok() && !status.complete())
      return;
    if (result.ok())
      result = status.operation_result;
    const auto& mirror = pending->second.mirror;
    log_texture(handle, result.ok() ? "ready" : "upload", result, mirror->variant, mirror->format,
                mirror->mips, mirror->bytes);
    if (result.ok() && mirror->data == data)
      texture_mirrors_.insert_or_assign(handle, mirror);
    pending_textures_.erase(pending);
    return;
  }
  pending_texture next;
  next.mirror = std::make_shared<texture_mirror>();
  next.mirror->data = std::move(data);
  const auto result =
      create_texture_mirror(*next.mirror->data, *next.mirror, handle, &next.operation);
  if (result.failed())
    return;
  if (next.operation.valid())
    pending_textures_.emplace(handle, std::move(next));
  else
    texture_mirrors_.insert_or_assign(handle, std::move(next.mirror));
}

void render_service::implementation::release_invalid_textures(
    const gneiss::engine::render_resource_snapshot& resources) noexcept {
  for (auto iterator = pending_textures_.begin(); iterator != pending_textures_.end();) {
    granit::async_operation_status status;
    const auto result = iterator->second.operation.get_status(status);
    if (!resources.get_texture(iterator->first) && (result.failed() || status.complete()))
      iterator = pending_textures_.erase(iterator);
    else
      ++iterator;
  }
  for (auto iterator = texture_mirrors_.begin(); iterator != texture_mirrors_.end();) {
    if (resources.get_texture(iterator->first) == nullptr) {
      iterator = texture_mirrors_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  // 旧纹理句柄不再用于绘制；下一次使用材质时重新解析全部槽位并更新绑定。
}

void render_service::implementation::release_invalid_meshes(
    const gneiss::engine::render_resource_snapshot& resources) noexcept {
  for (auto iterator = mesh_mirrors_.begin(); iterator != mesh_mirrors_.end();) {
    if (resources.get_mesh(iterator->first) == nullptr) {
      iterator = mesh_mirrors_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void render_service::implementation::release_invalid_materials(
    const gneiss::engine::render_resource_snapshot& resources) noexcept {
  for (auto iterator = material_mirrors_.begin(); iterator != material_mirrors_.end();) {
    if (resources.get_material(iterator->first) == nullptr) {
      iterator = material_mirrors_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

gneiss::engine::result render_service::implementation::initialize(
    const gneiss::engine::native_window_info& window, std::span<const std::byte> environment_asset,
    float environment_intensity, float environment_rotation_radians,
    gneiss::engine::log_dispatcher* log) noexcept {
  log_ = log;
  const auto executor_result =
      executor_.initialize([this](gneiss::engine::render_frame_packet& packet,
                                  gneiss::engine::render_execution_result& output) noexcept {
        const auto result = execute_frame(packet, output);
        output.needs_recreate = packet.window.needs_recreate;
        return result;
      });
  if (executor_result != gneiss::engine::result::success) {
    return executor_result;
  }

  gneiss::engine::result initialize_result = gneiss::engine::result::unknown;
  std::uint64_t sequence{};
  const auto submit_result = executor_.submit_command(
      [this, window, environment_asset, environment_intensity, environment_rotation_radians,
       &initialize_result](const gneiss::engine::render_command_reporter& reporter) {
        reporter.report(gneiss::engine::render_command_stage::uploading, 0U, 1U);
        initialize_result = initialize_gpu(window, environment_asset, environment_intensity,
                                           environment_rotation_radians);
        reporter.report(gneiss::engine::render_command_stage::uploading, 1U, 1U);
        return initialize_result;
      },
      sequence);
  if (submit_result == gneiss::engine::result::success) {
    static_cast<void>(executor_.flush());
  }

  gneiss::engine::render_command_completion completion;
  const auto has_completion = executor_.try_take_command_completion(completion);
  if (submit_result != gneiss::engine::result::success || !has_completion ||
      completion.sequence != sequence || completion.status != gneiss::engine::result::success) {
    granit::renderer_resource_stats ignored{};
    std::uint64_t cleanup_sequence{};
    if (executor_.submit_command(
            [this, &ignored](const gneiss::engine::render_command_reporter&) {
              return shutdown_gpu(ignored);
            },
            cleanup_sequence) == gneiss::engine::result::success) {
      static_cast<void>(executor_.flush());
    }
    executor_.stop();
    return submit_result != gneiss::engine::result::success
               ? submit_result
               : (has_completion ? completion.status : gneiss::engine::result::internal);
  }
  return initialize_result;
}

gneiss::engine::result render_service::implementation::collect_completions() noexcept {
  gneiss::engine::render_frame_completion completion;
  while (executor_.try_take_frame_completion(completion)) {
    if (recycled_frame_packets_.size() < 3U) {
      recycled_frame_packets_.push_back(std::move(completion.reusable_packet));
    }
    if (completion.policy == gneiss::engine::render_frame_policy::required) {
      required_frame_completions_.push_back({.sequence = completion.sequence,
                                             .status = completion.status,
                                             .execution = completion.execution,
                                             .dropped = completion.dropped,
                                             .policy = completion.policy,
                                             .reusable_packet = {}});
    }
    if (completion.dropped) {
      continue;
    }
    pending_recreate_ = pending_recreate_ || completion.execution.needs_recreate;
    if (completion.status != gneiss::engine::result::success) {
      return completion.status;
    }
  }
  return gneiss::engine::result::success;
}

gneiss::engine::result render_service::implementation::prepare_frame_packet_storage(
    gneiss::engine::render_frame_packet& packet, bool& out_should_prepare) noexcept {
  const auto completion_result = collect_completions();
  if (completion_result != gneiss::engine::result::success) {
    return completion_result;
  }
  // 交互帧只预备一帧：低帧率时积压三份旧相机快照会把输入延迟放大到数百毫秒。
  // 保留一份等待帧与正在执行的帧重叠；队列满时继续处理输入，空位出现后再捕获最新状态。
  out_should_prepare = executor_.query_stats().pending_frames < 1U;
  if (!out_should_prepare) {
    executor_.record_skipped_frame_build();
    return gneiss::engine::result::success;
  }
  if (!recycled_frame_packets_.empty()) {
    packet = std::move(recycled_frame_packets_.back());
    recycled_frame_packets_.pop_back();
  }
  return gneiss::engine::result::success;
}

gneiss::engine::result
render_service::implementation::submit(gneiss::engine::render_frame_packet packet,
                                       gneiss::engine::render_frame_policy policy,
                                       std::uint64_t* out_sequence) noexcept {
  const auto completion_result = collect_completions();
  if (completion_result != gneiss::engine::result::success) {
    return completion_result;
  }
  packet.window.needs_recreate = packet.window.needs_recreate || pending_recreate_;
  pending_recreate_ = false;
  std::uint64_t sequence{};
  const auto result = executor_.submit_frame(std::move(packet), sequence, policy);
  if (result == gneiss::engine::result::success && out_sequence != nullptr) {
    *out_sequence = sequence;
  }
  return result;
}

bool render_service::implementation::try_take_required_completion(
    gneiss::engine::render_frame_completion& completion) noexcept {
  if (collect_completions() != gneiss::engine::result::success ||
      required_frame_completions_.empty()) {
    return false;
  }
  completion = std::move(required_frame_completions_.front());
  required_frame_completions_.pop_front();
  return true;
}

gneiss::engine::render_queue_stats
render_service::implementation::query_performance_stats() const noexcept {
  return executor_.query_stats();
}

gneiss::engine::result render_service::implementation::finish_frames() noexcept {
  const auto flushed = executor_.flush();
  if (flushed != gneiss::engine::result::success) {
    return flushed;
  }
  auto result = gneiss::engine::result::success;
  for (auto completed = collect_completions(); completed != gneiss::engine::result::success;
       completed = collect_completions()) {
    if (result == gneiss::engine::result::success &&
        completed != gneiss::engine::result::not_ready) {
      result = completed;
    }
  }
  return result;
}

gneiss::engine::result
render_service::implementation::shutdown(granit::renderer_resource_stats& stats) noexcept {
  if (!executor_.is_running()) {
    return gneiss::engine::result::not_ready;
  }
  static_cast<void>(executor_.flush());
  const auto frame_result = collect_completions();

  std::uint64_t sequence{};
  const auto submit_result = executor_.submit_command(
      [this, &stats](const gneiss::engine::render_command_reporter& reporter) {
        reporter.report(gneiss::engine::render_command_stage::uploading, 0U, 1U);
        const auto result = shutdown_gpu(stats);
        reporter.report(gneiss::engine::render_command_stage::uploading, 1U, 1U);
        return result;
      },
      sequence);
  if (submit_result == gneiss::engine::result::success) {
    static_cast<void>(executor_.flush());
  }
  gneiss::engine::render_command_completion completion;
  const auto has_completion = executor_.try_take_command_completion(completion);
  executor_.stop();
  recycled_frame_packets_.clear();
  required_frame_completions_.clear();
  if (frame_result != gneiss::engine::result::success) {
    return frame_result;
  }
  if (submit_result != gneiss::engine::result::success) {
    return submit_result;
  }
  if (!has_completion || completion.sequence != sequence) {
    return gneiss::engine::result::internal;
  }
  return completion.status;
}

gneiss::engine::result render_service::implementation::initialize_gpu(
    const gneiss::engine::native_window_info& window, std::span<const std::byte> environment_asset,
    float environment_intensity, float environment_rotation_radians) noexcept {
  auto result = renderer_.initialize({.application_name = "Gneiss",
                                      .enable_validation = false,
                                      .presentation = granit::presentation_mode::enabled});
  if (result.failed()) {
    return map_result(result);
  }

  static std::atomic<std::uint64_t> device_generation{0U};
  texture_profile_ = {};
  constexpr std::array formats{
      granit::texture_format::rgba8_unorm, granit::texture_format::rgba8_srgb,
      granit::texture_format::bc7_rgba_unorm, granit::texture_format::bc7_rgba_srgb};
  for (std::size_t index = 0U; index < formats.size(); ++index) {
    granit::texture_format_capabilities capabilities;
    result = granit::get_texture_format_capabilities(renderer_, formats[index], capabilities);
    if (result.failed()) {
      return map_result(result);
    }
    texture_profile_.sampled_transfer_formats[index] = capabilities.supports(
        granit::texture_usage::sampled | granit::texture_usage::transfer_destination);
  }
  texture_profile_.generation = device_generation.fetch_add(1U, std::memory_order_relaxed) + 1U;

  switch (window.backend) {
  case gneiss::engine::native_window_backend::win32:
    result =
        surface_.initialize(renderer_, granit::surface_desc::win32(window.display, window.window));
    break;
  case gneiss::engine::native_window_backend::xcb:
    result = surface_.initialize(renderer_,
                                 granit::surface_desc::xcb(window.display, window.xcb_window));
    break;
  case gneiss::engine::native_window_backend::wayland:
    result = surface_.initialize(renderer_,
                                 granit::surface_desc::wayland(window.display, window.window));
    break;
  default:
    return gneiss::engine::result::unsupported;
  }
  if (result.ok()) {
    result = swapchain_.initialize(renderer_, surface_,
                                   {.width = window.width, .height = window.height});
  }
  granit::swapchain_info swapchain_info;
  if (result.ok()) {
    result = swapchain_.query_info(swapchain_info);
  }
  if (result.ok()) {
    swapchain_format_ = swapchain_info.format;
    result = initialize_pipeline();
    if (result.ok()) {
      environment_asset_requested_ = !environment_asset.empty();
      environment_fallback_ = false;
      if (!environment_asset.empty()) {
        result = environment_.initialize(renderer_, environment_asset);
      }
      if (environment_asset.empty() || result.failed()) {
        environment_fallback_ = !environment_asset.empty();
        static_cast<void>(environment_.reset());
        result = environment_.initialize_builtin(renderer_);
      }
    }
    if (result.ok()) {
      result = environment_.get_info(environment_info_);
    }
    if (result.ok()) {
      environment_info_.environment.intensity = environment_intensity;
      environment_info_.environment.rotation_radians = environment_rotation_radians;
      environment_intensity_ = environment_intensity;
      environment_rotation_radians_ = environment_rotation_radians;
    }
  }
  if (result.ok()) {
    result = ensure_default_textures();
  }
  if (result.ok()) {
    result = ui_sampler_.initialize(renderer_, {.mag_filter = granit::filter::linear,
                                                .min_filter = granit::filter::linear,
                                                .mip_filter = granit::mipmap_filter::nearest,
                                                .address_u = granit::address_mode::clamp_to_edge,
                                                .address_v = granit::address_mode::clamp_to_edge,
                                                .address_w = granit::address_mode::clamp_to_edge,
                                                .max_lod = 0.0F});
  }
  if (result.ok()) {
    const granit::canvas_draw_list_desc desc{};
    result = ui_canvas_.initialize(renderer_, desc);
  }
  if (result.ok()) {
    const granit::debug_draw_list_desc desc{};
    result = debug_draw_.initialize(renderer_, desc);
  }
  return map_result(result);
}

gneiss::engine::result
render_service::implementation::shutdown_gpu(granit::renderer_resource_stats& stats) noexcept {
  static_cast<void>(pipeline_.reset());
  static_cast<void>(environment_.reset());
  environment_info_ = {};
  material_mirrors_.clear();
  mesh_mirrors_.clear();
  texture_mirrors_.clear();
  for (auto& [handle, pending] : pending_textures_) {
    (void)handle;
    granit::async_operation_status status;
    while (pending.operation.get_status(status).ok() && !status.complete()) {
      if (renderer_.process_events().failed())
        break;
      std::this_thread::yield();
    }
  }
  pending_textures_.clear();
  static_cast<void>(default_normal_linear_.view.reset());
  static_cast<void>(default_normal_linear_.texture.reset());
  static_cast<void>(default_white_linear_.view.reset());
  static_cast<void>(default_white_linear_.texture.reset());
  static_cast<void>(default_white_srgb_.view.reset());
  static_cast<void>(default_white_srgb_.texture.reset());
  static_cast<void>(ui_canvas_.destroy());
  static_cast<void>(debug_draw_.destroy());
  static_cast<void>(ui_sampler_.reset());
  // 材质和管线先销毁，再释放借用归档内存的 Shader Library。
  const auto library_result = pbr_library_.reset();
  if (library_result.ok())
    pbr_assets_.reset();
  last_pipeline_metric_sequence_ = 0;
  gpu_timing_supported_ = false;
  environment_asset_requested_ = false;
  environment_fallback_ = false;
  environment_intensity_ = 1.0F;
  environment_rotation_radians_ = 0.0F;
  static_cast<void>(swapchain_.reset());
  static_cast<void>(surface_.reset());

  const auto query_result = renderer_.get_resource_stats(stats);
  const auto result = query_result.failed()
                          ? map_result(query_result)
                          : (stats.total_live_count == 0U ? gneiss::engine::result::success
                                                          : gneiss::engine::result::invalid_state);
  static_cast<void>(renderer_.reset());
  return result;
}

// 单帧资源准备与提交必须保持 Granit 调用和失败回滚的线性顺序。
gneiss::engine::result
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
render_service::implementation::execute_frame(
    gneiss::engine::render_frame_packet& packet,
    gneiss::engine::render_execution_result& output) noexcept {
  output.gpu_timing_supported = gpu_timing_supported_;
  output.environment_asset_requested = environment_asset_requested_;
  output.environment_fallback = environment_fallback_;
  output.environment_intensity = environment_intensity_;
  output.environment_rotation_radians = environment_rotation_radians_;
  auto& window = packet.window;
  const auto& snapshot = packet.scene;
  const auto& resources = packet.resources;
  if (window.width == 0U || window.height == 0U)
    return gneiss::engine::result::success;

  const auto resource_prepare_started = std::chrono::steady_clock::now();
  if (window.needs_recreate) {
    const auto recreate = swapchain_.recreate({.width = window.width, .height = window.height});
    if (recreate == granit::result::not_ready)
      return gneiss::engine::result::success;
    if (recreate.failed())
      return map_result(recreate);
    granit::swapchain_info info;
    const auto query = swapchain_.query_info(info);
    if (query.failed())
      return map_result(query);
    swapchain_format_ = info.format;
    window.needs_recreate = false;
  }

  release_invalid_textures(resources);
  release_invalid_materials(resources);
  release_invalid_meshes(resources);
  try {
    for (const auto& instance : snapshot.instances) {
      if (resources.mesh_pending(resource_handle(instance.mesh))) {
        continue;
      }
      const auto handle = resource_handle(instance.mesh);
      auto source = resources.share_mesh(handle);
      const auto current = mesh_mirrors_.find(handle);
      if (source && (current == mesh_mirrors_.end() || current->second.data != source)) {
        mesh_mirror replacement;
        const auto created = create_mesh_mirror(*source, replacement);
        if (created.ok()) {
          replacement.data = std::move(source);
          mesh_mirrors_.insert_or_assign(handle, std::move(replacement));
        } else if (log_) {
          (void)log_->submit({.severity = level::error,
                              .source = "render.mesh",
                              .category = "render.resource",
                              .message = "网格上传失败，保留当前可用内容",
                              .result = map_result(created)});
        }
      }
    }

    for (const auto& instance : snapshot.instances) {
      if (resources.mesh_pending(resource_handle(instance.mesh))) {
        continue;
      }
      const auto* material = resources.get_material(resource_handle(instance.material));
      if (material == nullptr)
        return gneiss::engine::result::invalid_handle;
      std::array textures{
          default_white_srgb_.view.native_handle(), default_white_linear_.view.native_handle(),
          default_normal_linear_.view.native_handle(), default_white_linear_.view.native_handle(),
          default_white_srgb_.view.native_handle()};
      const auto handles = material->texture_handles();
      for (std::size_t slot = 0; slot < handles.size(); ++slot) {
        const auto handle = handles[slot];
        if (handle == gneiss::engine::null_rid)
          continue;
        const auto* texture = resources.get_texture(handle);
        if (resources.texture_pending(handle)) {
          continue;
        }
        if (texture == nullptr)
          return gneiss::engine::result::invalid_handle;
        refresh_texture(handle, resources.share_texture(handle));
        const auto found = texture_mirrors_.find(handle);
        if (found != texture_mirrors_.end())
          textures[slot] = found->second->view.native_handle();
      }
      auto found = material_mirrors_.find(resource_handle(instance.material));
      if (found == material_mirrors_.end() || found->second.source != material ||
          found->second.textures != textures) {
        material_mirror replacement;
        const auto created = create_material_mirror(*material, textures, replacement);
        if (created.ok()) {
          replacement.data = resources.share_material(resource_handle(instance.material));
          for (std::size_t slot = 0; slot < handles.size(); ++slot) {
            const auto dependency = texture_mirrors_.find(handles[slot]);
            if (dependency != texture_mirrors_.end())
              replacement.dependencies[slot] = dependency->second;
          }
          material_mirrors_.insert_or_assign(resource_handle(instance.material),
                                             std::move(replacement));
        } else if (log_) {
          (void)log_->submit({.severity = level::error,
                              .source = "render.material",
                              .category = "render.resource",
                              .message = "材质更新失败，保留当前可用内容",
                              .result = map_result(created)});
        }
      }
    }
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }

  const auto ui_result = prepare_ui_draw_list(packet.ui, resources, window.width, window.height);
  if (ui_result.failed())
    return map_result(ui_result);
  auto result = debug_draw_.clear();
  if (result.failed())
    return map_result(result);
  try {
    std::vector<granit::debug_draw_line> lines;
    lines.reserve(packet.debug.lines().size());
    for (const auto& line : packet.debug.lines()) {
      lines.push_back(
          {.start = {.x = line.start[0],
                     .y = line.start[1],
                     .z = line.start[2],
                     .color = line.color_rgba8},
           .end = {.x = line.end[0], .y = line.end[1], .z = line.end[2], .color = line.color_rgba8},
           .width = line.width,
           .space = granit::debug_draw_space::world,
           .depth_mode = line.depth_test != 0U ? granit::debug_draw_depth_mode::test
                                               : granit::debug_draw_depth_mode::disabled});
    }
    if (!lines.empty()) {
      result = debug_draw_.append_lines(lines);
      if (result.failed())
        return map_result(result);
    }
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  }

  granit::scene_snapshot scene;
  std::vector<granit_scene_renderable> renderables;
  std::vector<granit::render_pipeline_draw_binding> bindings;
  granit_scene_view view{};
  view.view.elements[0] = 1.0F;
  view.view.elements[5] = 1.0F;
  view.view.elements[10] = 1.0F;
  view.view.elements[15] = 1.0F;
  view.projection = view.view;
  view.view_projection = view.view;
  view.viewport_width = static_cast<float>(window.width);
  view.viewport_height = static_cast<float>(window.height);
  view.layer_mask = UINT64_MAX;
  granit_scene_directional_light light{{-0.35F, 0.8F, 0.45F}, {3.0F, 3.0F, 3.0F}, UINT64_MAX};
  if (snapshot.has_camera) {
    view.view = to_granit_matrix(snapshot.camera.view);
    view.projection = to_granit_matrix(snapshot.camera.projection);
    view.view_projection =
        to_granit_matrix(multiply(snapshot.camera.projection, snapshot.camera.view));
    view.camera_position = {snapshot.camera.transform.translation[0],
                            snapshot.camera.transform.translation[1],
                            snapshot.camera.transform.translation[2]};
    view.viewport_width = static_cast<float>(window.width);
    view.viewport_height = static_cast<float>(window.height);
    view.layer_mask = UINT64_MAX;
    try {
      renderables.reserve(snapshot.instances.size());
      bindings.reserve(snapshot.instances.size());
      for (std::size_t index = 0; index < snapshot.instances.size(); ++index) {
        const auto& instance = snapshot.instances[index];
        if (resources.mesh_pending(resource_handle(instance.mesh))) {
          continue;
        }
        gneiss::engine::matrix4 model;
        gneiss::engine::matrix4 normal;
        if (!build_model_matrices(instance.transform, model, normal))
          return gneiss::engine::result::invalid_argument;
        const auto payload = static_cast<std::uint64_t>(index) + 1U;
        const auto gpu_mesh = mesh_mirrors_.find(resource_handle(instance.mesh));
        const auto gpu_material = material_mirrors_.find(resource_handle(instance.material));
        if (gpu_mesh == mesh_mirrors_.end() || gpu_material == material_mirrors_.end())
          continue;
        const auto* mesh = gpu_mesh->second.source;
        const auto* material = gpu_material->second.source;
        if (material->uv1_mask() != 0U && mesh->uv1.empty())
          return gneiss::engine::result::invalid_argument;
        // GPU 布局统一包含 UV1 与颜色；缺失属性填零 UV / 白色，变体必须匹配 72 字节步长。
        const auto variant = granit_pbr_material_variant_key(
            GRANIT_PBR_TEXTURE_ALL, static_cast<std::uint32_t>(material->parameters.alpha_mode),
            material->parameters.double_sided, 1U, 1U);
        renderables.push_back(
            {.model = to_granit_matrix(model),
             .normal_matrix = to_granit_matrix(normal),
             .bounds_center = {instance.transform.translation[0], instance.transform.translation[1],
                               instance.transform.translation[2]},
             .bounds_radius = mesh_bounds_radius(*mesh, instance.transform),
             .layer_mask = UINT64_MAX,
             .sort_key = resource_handle(instance.material),
             .payload = payload,
             .object_id = static_cast<std::uint32_t>(index + 1U),
             .reserved = 0});
        bindings.push_back(
            {.payload = payload,
             .mesh = mesh_mirrors_.at(resource_handle(instance.mesh)).mesh.ref(),
             .material = material_mirrors_.at(resource_handle(instance.material)).material.ref(),
             .variant = variant});
      }
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    }
  }
  granit::scene_snapshot_desc scene_desc{};
  scene_desc.views = {&view, 1};
  if (snapshot.has_camera) {
    scene_desc.renderables = renderables;
    scene_desc.directional_lights = {&light, 1};
  }
  result = scene.initialize(renderer_, scene_desc);
  if (result.failed())
    return map_result(result);
  output.resource_prepare_ms = std::chrono::duration<float, std::milli>(
                                   std::chrono::steady_clock::now() - resource_prepare_started)
                                   .count();

  if (packet.readback) {
    try {
      granit::texture target;
      auto captured =
          target.initialize(renderer_, {.format = granit::texture_format::rgba8_unorm,
                                        .usage = granit::texture_usage::color_attachment |
                                                 granit::texture_usage::transfer_source,
                                        .width = window.width,
                                        .height = window.height});
      granit::texture_view target_view;
      if (captured.ok()) {
        captured = target_view.initialize(renderer_, target,
                                          {.format = granit::texture_format::rgba8_unorm});
      }
      if (captured.ok()) {
        granit::render_pipeline_render_desc desc{};
        desc.scene = scene.ref();
        desc.output = target_view.ref();
        desc.output_format = granit::texture_format::rgba8_unorm;
        desc.width = window.width;
        desc.height = window.height;
        desc.draw_bindings = bindings;
        desc.canvas = ui_canvas_.ref();
        desc.debug_draw = debug_draw_.ref();
        desc.clear_color = {0.04F, 0.12F, 0.22F, 1.0F};
        desc.environment = &environment_info_.environment;
        captured = pipeline_.render(desc);
      }
      granit::readback_batch batch;
      const auto byte_count = static_cast<std::uint64_t>(window.width) * window.height * 4U;
      if (captured.ok()) {
        captured =
            batch.create(renderer_, {.max_result_bytes = byte_count, .max_operation_count = 1U});
      }
      std::uint32_t index{};
      if (captured.ok()) {
        captured = batch.read_texture(target.ref(),
                                      {.width = window.width, .height = window.height}, index);
      }
      granit::async_operation operation;
      if (captured.ok()) {
        captured = batch.submit_async(operation);
      }
      granit::async_operation_status status;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (captured.ok()) {
        captured = operation.get_status(status);
        if (captured.failed() || status.complete()) {
          break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
          return gneiss::engine::result::not_ready;
        }
        captured = renderer_.process_events();
        std::this_thread::yield();
      }
      if (captured.ok()) {
        captured = status.operation_result;
      }
      granit::readback_result_info info;
      if (captured.ok()) {
        captured = granit::get_readback_result_info(operation, index, info);
      }
      if (captured.failed()) {
        return map_result(captured);
      }
      if (info.required_size != byte_count || info.bytes_per_row != window.width * 4U ||
          info.format != granit::texture_format::rgba8_unorm) {
        return gneiss::engine::result::internal;
      }
      packet.readback->pixels.resize(static_cast<std::size_t>(byte_count));
      std::uint64_t required{};
      captured = granit::copy_readback_result(operation, index, packet.readback->pixels, required);
      if (captured.failed()) {
        return map_result(captured);
      }
      packet.readback->width = window.width;
      packet.readback->height = window.height;
      return gneiss::engine::result::success;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }

  granit::acquired_frame frame;
  const auto acquire_started = std::chrono::steady_clock::now();
  result = swapchain_.acquire(frame);
  output.acquire_wait_ms =
      std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - acquire_started)
          .count();
  if (result == granit::result::out_of_date) {
    window.needs_recreate = true;
    return gneiss::engine::result::success;
  }
  if (result.failed())
    return map_result(result);
  window.needs_recreate = window.needs_recreate || frame.needs_recreate();

  granit::swapchain_backbuffer backbuffer;
  result = swapchain_.backbuffer(frame, backbuffer);
  if (result.ok()) {
    granit::swapchain_info actual;
    result = swapchain_.query_info(actual);
    if (result.ok() && (actual.width != window.width || actual.height != window.height)) {
      // 连续 resize 时，排队帧的窗口尺寸可能已经落后于实际 Backbuffer。
      // 不将旧尺寸传给管线；归还已获取帧，下一帧按最新窗口事件重建。
      const auto cancelled = swapchain_.cancel(frame);
      window.needs_recreate = true;
      output.needs_recreate = true;
      return map_result(cancelled);
    }
  }
  const auto render_started = std::chrono::steady_clock::now();
  if (result.ok()) {
    granit::render_pipeline_render_desc desc{};
    desc.scene = scene.ref();
    desc.output = backbuffer.view;
    desc.output_format = swapchain_format_;
    desc.width = window.width;
    desc.height = window.height;
    desc.draw_bindings = bindings;
    desc.frame = &frame;
    desc.canvas = ui_canvas_.ref();
    desc.debug_draw = debug_draw_.ref();
    desc.clear_color = {0.04F, 0.12F, 0.22F, 1.0F};
    desc.environment = &environment_info_.environment;
    result = pipeline_.render(desc);
  }
  output.record_submit_ms =
      std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - render_started)
          .count();
  if (result.ok()) {
    if (gpu_timing_supported_) {
      granit::render_pipeline_metrics metrics{};
      const auto metric_result = pipeline_.get_metrics(metrics);
      if (metric_result.ok() && metrics.sample_sequence > last_pipeline_metric_sequence_) {
        output.gpu_timing_valid = true;
        output.gpu_timing_sample_sequence = metrics.sample_sequence;
        output.gpu_frame_ms = static_cast<float>(metrics.total_gpu_ns) / 1'000'000.0F;
        output.gpu_shadow_ms = static_cast<float>(metrics.shadow_gpu_ns) / 1'000'000.0F;
        output.gpu_opaque_ms = static_cast<float>(metrics.opaque_gpu_ns) / 1'000'000.0F;
        output.gpu_tone_mapping_ms = static_cast<float>(metrics.tone_mapping_gpu_ns) / 1'000'000.0F;
        last_pipeline_metric_sequence_ = metrics.sample_sequence;
      } else if (metric_result.failed() && metric_result != granit::result::not_ready) {
        gpu_timing_supported_ = false;
        output.gpu_timing_supported = false;
      }
    }
    const auto present_started = std::chrono::steady_clock::now();
    result = swapchain_.present(frame);
    output.presented = result.ok();
    output.present_wait_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - present_started)
            .count();
  } else if (frame.valid()) {
    static_cast<void>(swapchain_.cancel(frame));
  }
  if (result == granit::result::out_of_date) {
    window.needs_recreate = true;
    result = granit::result::success;
  }
  output.needs_recreate = window.needs_recreate;
  return map_result(result);
}

render_service::render_service() : impl_(std::make_unique<implementation>()) {}
render_service::~render_service() = default;
render_resource_service& render_service::resources() noexcept { return impl_->resources_; }
result render_service::initialize(const native_window_info& window,
                                  std::span<const std::byte> environment_asset,
                                  float environment_intensity, float environment_rotation_radians,
                                  log_dispatcher* log) noexcept {
  return impl_->initialize(window, environment_asset, environment_intensity,
                           environment_rotation_radians, log);
}
result render_service::shutdown(render_device_stats& stats) noexcept {
  granit::renderer_resource_stats native{
      .total_live_count = stats.total_live_count,
      .buffer_count = stats.buffer_count,
      .texture_count = stats.texture_count,
      .texture_view_count = stats.texture_view_count,
      .sampler_count = stats.sampler_count,
      .shader_count = stats.shader_count,
      .bind_group_layout_count = stats.bind_group_layout_count,
      .bind_group_count = stats.bind_group_count,
      .pipeline_layout_count = stats.pipeline_layout_count,
      .graphics_pipeline_count = stats.graphics_pipeline_count,
      .compute_pipeline_count = stats.compute_pipeline_count,
      .surface_count = stats.surface_count,
      .swapchain_count = stats.swapchain_count,
      .command_recorder_count = stats.command_recorder_count,
      .frame_context_count = stats.frame_context_count,
      .frame_count = stats.frame_count,
      .timestamp_query_pool_count = stats.timestamp_query_pool_count,
      .upload_batch_count = stats.upload_batch_count,
      .pending_retirement_count = stats.pending_retirement_count,
      .async_operation_count = stats.async_operation_count,
      .readback_batch_count = stats.readback_batch_count,
  };
  const auto status = impl_->shutdown(native);
  if (status.ok())
    impl_->resources_.close();
  stats = {
      .total_live_count = native.total_live_count,
      .buffer_count = native.buffer_count,
      .texture_count = native.texture_count,
      .texture_view_count = native.texture_view_count,
      .sampler_count = native.sampler_count,
      .shader_count = native.shader_count,
      .bind_group_layout_count = native.bind_group_layout_count,
      .bind_group_count = native.bind_group_count,
      .pipeline_layout_count = native.pipeline_layout_count,
      .graphics_pipeline_count = native.graphics_pipeline_count,
      .compute_pipeline_count = native.compute_pipeline_count,
      .surface_count = native.surface_count,
      .swapchain_count = native.swapchain_count,
      .command_recorder_count = native.command_recorder_count,
      .frame_context_count = native.frame_context_count,
      .frame_count = native.frame_count,
      .timestamp_query_pool_count = native.timestamp_query_pool_count,
      .upload_batch_count = native.upload_batch_count,
      .pending_retirement_count = native.pending_retirement_count,
      .async_operation_count = native.async_operation_count,
      .readback_batch_count = native.readback_batch_count,
  };
  return status;
}
result render_service::finish_frames() noexcept { return impl_->finish_frames(); }
void render_service::set_completion_notification(
    std::shared_ptr<progress_notification> notification) noexcept {
  impl_->set_completion_notification(std::move(notification));
}
result render_service::prepare_frame_packet_storage(render_frame_packet& packet,
                                                    bool& out_should_prepare) noexcept {
  return impl_->prepare_frame_packet_storage(packet, out_should_prepare);
}
result render_service::submit(render_frame_packet packet, render_frame_policy policy,
                              std::uint64_t* out_sequence) noexcept {
  return impl_->submit(std::move(packet), policy, out_sequence);
}
bool render_service::try_take_required_completion(render_frame_completion& completion) noexcept {
  return impl_->try_take_required_completion(completion);
}
render_queue_stats render_service::query_performance_stats() const noexcept {
  return impl_->query_performance_stats();
}
texture_prepare_profile render_service::texture_profile() const noexcept {
  return impl_->texture_profile();
}

} // namespace gneiss::engine
