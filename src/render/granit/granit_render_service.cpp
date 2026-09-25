// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/granit_render_service.h"

#include "log/log_dispatcher.h"

#include <granit/core/version.h>
#include <granit/renderer/native_surface.hpp>
#include <granit/renderer/readback_batch.hpp>
#include <granit/renderer/texture_asset.hpp>
#include <granit/renderer/upload_batch.hpp>
#include <thread>

#include <granit/pipeline/pbr_material.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <vector>

namespace gneiss::application_internal {
namespace {

static_assert(GRANIT_VERSION_MAJOR > 0 || GRANIT_VERSION_MINOR >= 30,
              "Gneiss requires Granit 0.30.0 or newer");

gneiss_result map_result(granit::result result) noexcept {
  switch (result.native()) {
  case granit::result::success.native():
    return GNEISS_SUCCESS;
  case granit::result::invalid_argument.native():
    return GNEISS_ERROR_INVALID_ARGUMENT;
  case granit::result::invalid_handle.native():
    return GNEISS_ERROR_INVALID_HANDLE;
  case granit::result::out_of_memory.native():
    return GNEISS_ERROR_OUT_OF_MEMORY;
  case granit::result::unsupported.native():
  case granit::result::backend_unavailable.native():
    return GNEISS_ERROR_UNSUPPORTED;
  case granit::result::not_ready.native():
  case granit::result::out_of_date.native():
    return GNEISS_ERROR_NOT_READY;
  case granit::result::initialization_failed.native():
  case granit::result::incompatible_driver.native():
    return GNEISS_ERROR_INITIALIZATION_FAILED;
  default:
    return GNEISS_ERROR_DEPENDENCY_FAILED;
  }
}

struct gpu_vertex {
  std::array<float, 3> position;
  std::array<float, 3> normal;
  std::array<float, 4> tangent;
  std::array<float, 2> texture_coordinate;
};

struct geometry_range final {
  std::uint32_t first_index{};
  std::int32_t vertex_offset{};
  std::uint32_t index_count{};
};

render_internal::matrix4 multiply(const render_internal::matrix4& left,
                                  const render_internal::matrix4& right) noexcept {
  render_internal::matrix4 result;
  for (std::size_t row = 0; row < 4U; ++row) {
    for (std::size_t column = 0; column < 4U; ++column) {
      for (std::size_t inner = 0; inner < 4U; ++inner) {
        result.values[(column * 4U) + row] +=
            left.values[(inner * 4U) + row] * right.values[(column * 4U) + inner];
      }
    }
  }
  return result;
}

granit_matrix4 to_granit_matrix(const render_internal::matrix4& source) noexcept {
  granit_matrix4 result{};
  std::ranges::copy(source.values, result.elements);
  return result;
}

float mesh_bounds_radius(const render_internal::mesh_resource& mesh,
                         const gneiss_transform& transform) noexcept {
  float radius_squared = 0.0F;
  for (const auto& vertex : mesh.vertices) {
    radius_squared =
        std::max(radius_squared, vertex.x * vertex.x + vertex.y * vertex.y + vertex.z * vertex.z);
  }
  const auto scale = std::max(
      {std::abs(transform.scale[0]), std::abs(transform.scale[1]), std::abs(transform.scale[2])});
  return std::sqrt(radius_squared) * scale;
}

granit::result append_mesh_geometry(const render_internal::mesh_resource& source,
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
    const auto normal =
        source.normals.empty() ? gneiss_mesh_normal{0.0F, 1.0F, 0.0F} : source.normals[index];
    vertices.push_back({.position = {vertex.x, vertex.y, vertex.z},
                        .normal = {normal.x, normal.y, normal.z},
                        .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
                        .texture_coordinate = {vertex.u, vertex.v}});
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

granit::result granit_render_service::initialize_pipeline() noexcept {
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

void granit_render_service::log_texture(gneiss_texture rid, const char* stage,
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
  gneiss_log_message message = GNEISS_LOG_MESSAGE_INIT;
  message.severity = result.ok() ? GNEISS_LOG_INFO : GNEISS_LOG_ERROR;
  message.category = category.data();
  message.category_length = category.size();
  message.message = buffer.data();
  message.message_length = std::min(static_cast<std::size_t>(length), buffer.size() - 1U);
  message.result = map_result(result);
  static_cast<void>(log_->submit(log_application_.load(std::memory_order_relaxed), message,
                                 "granit.render.texture"));
}

granit::result
granit_render_service::create_texture_mirror(const render_internal::texture_resource& source,
                                             texture_mirror& output, gneiss_texture rid) noexcept {
  if (auto found = prepared_textures_.find(&source); found != prepared_textures_.end()) {
    output = std::move(found->second.mirror);
    prepared_textures_.erase(found);
    return granit::result::success;
  }
  output.source = &source;
  if (!source.manifest.empty()) {
    granit::texture_asset_info info;
    granit::texture_asset_selection selection;
    auto result = granit::inspect_texture_asset(source.manifest, info);
    const char* stage = "inspect";
    if (result.ok()) {
      stage = "select";
      result = granit::select_texture_asset_variant(renderer_, source.manifest, selection);
    }
    if (result.failed() || selection.variant_index >= info.variants.size()) {
      output.source = nullptr;
      result = result.failed() ? result : granit::result::invalid_argument;
      log_texture(rid, stage, result, selection.variant_index, selection.format, info.mip_levels,
                  0U);
      return result;
    }
    const auto& variant = info.variants[selection.variant_index];
    const auto format = static_cast<granit::texture_format>(selection.format);
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
      result = granit::write_texture_asset_mips(upload, output.texture.ref(), source.manifest,
                                                source.payload, selection.variant_index, 0U,
                                                info.mip_levels);
    }
    if (result.ok()) {
      stage = "submit";
      result = upload.submit();
    }
    if (result.ok()) {
      stage = "view";
      result = output.view.initialize(renderer_, output.texture, {.format = format});
    }
    if (result.failed()) {
      static_cast<void>(output.view.reset());
      static_cast<void>(output.texture.reset());
      output.source = nullptr;
    }
    log_texture(rid, result.ok() ? "ready" : stage, result, selection.variant_index, format,
                info.mip_levels, variant.payload_size);
    return result;
  }
  const auto format = source.color_space == GNEISS_TEXTURE_COLOR_SPACE_SRGB
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
    result = upload.submit();
  }
  if (result.ok()) {
    result = output.view.initialize(renderer_, output.texture, {.format = format});
  }
  if (result.failed()) {
    static_cast<void>(output.view.reset());
    static_cast<void>(output.texture.reset());
    output.source = nullptr;
  }
  return result;
}

granit::result
granit_render_service::create_material_mirror(const render_internal::material_resource& source,
                                              granit_texture_view base_color,
                                              material_mirror& output) noexcept {
  const std::array color{source.red, source.green, source.blue, source.alpha};
  constexpr float normal_scale = 1.0F;
  constexpr float occlusion_strength = 1.0F;
  constexpr std::array emissive{0.0F, 0.0F, 0.0F};
  constexpr std::uint32_t debug_display = 0;
  const std::array updates{
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_BASE_COLOR),
          granit::material_parameter_type::float4,
          std::as_bytes(std::span{color.data(), color.size()})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_METALLIC),
          granit::material_parameter_type::float32, std::as_bytes(std::span{&source.metallic, 1})),
      granit::material_parameter_update::value(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_PERCEPTUAL_ROUGHNESS),
          granit::material_parameter_type::float32, std::as_bytes(std::span{&source.roughness, 1})),
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
          granit::texture_view_ref::from_native(base_color)),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_METALLIC_ROUGHNESS_TEXTURE),
          default_white_linear_.view.ref()),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_NORMAL_TEXTURE),
          default_normal_linear_.view.ref()),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_OCCLUSION_TEXTURE),
          default_white_linear_.view.ref()),
      granit::material_parameter_update::texture_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_EMISSIVE_TEXTURE),
          default_white_srgb_.view.ref()),
      granit::material_parameter_update::sampler_binding(
          granit::material_parameter_id(GRANIT_PBR_PARAMETER_SAMPLER), sampler_.ref())};
  const granit::material_desc desc{.archive = pbr_shader_resolver::material_archive(),
                                   .initial_updates = updates,
                                   .shader_library = pbr_library_.ref()};
  auto result = output.material.initialize(renderer_, desc);
  if (result.ok()) {
    output.source = &source;
    output.base_color_texture = source.base_color_texture;
  }
  return result;
}

granit::result granit_render_service::rebuild_geometry_arena(
    const render_internal::render_resource_snapshot& resources) noexcept {
  try {
    if (mesh_mirrors_.empty()) {
      static_cast<void>(geometry_vertices_.reset());
      static_cast<void>(geometry_indices_.reset());
      geometry_dirty_ = false;
      return granit::result::success;
    }
    std::vector<gpu_vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (auto& [rid, mirror] : mesh_mirrors_) {
      const auto* source = resources.get_mesh(rid);
      if (source == nullptr) {
        return granit::result::invalid_handle;
      }
      geometry_range range;
      const auto append_result = append_mesh_geometry(*source, vertices, indices, range);
      if (append_result.failed()) {
        return append_result;
      }
      mirror.first_index = range.first_index;
      mirror.vertex_offset = range.vertex_offset;
      mirror.index_count = range.index_count;
    }

    for (auto& [rid, mirror] : mesh_mirrors_) {
      static_cast<void>(rid);
      static_cast<void>(mirror.mesh.reset());
    }
    granit::buffer replacement_vertices;
    granit::buffer replacement_indices;
    auto result = replacement_vertices.initialize(
        renderer_,
        {.size = vertices.size() * sizeof(gpu_vertex), .usage = granit::buffer_usage::vertex},
        std::as_bytes(std::span{vertices}));
    if (result.ok()) {
      result = replacement_indices.initialize(
          renderer_,
          {.size = indices.size() * sizeof(std::uint32_t), .usage = granit::buffer_usage::index},
          std::as_bytes(std::span{indices}));
    }
    if (result.failed()) {
      return result;
    }
    geometry_vertices_ = std::move(replacement_vertices);
    geometry_indices_ = std::move(replacement_indices);
    const std::array attributes{
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_POSITION, GRANIT_VERTEX_FORMAT_FLOAT32X3,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, position)), 0},
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_NORMAL, GRANIT_VERTEX_FORMAT_FLOAT32X3,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, normal)), 0},
        granit_vertex_attribute{GRANIT_PBR_VERTEX_LOCATION_TANGENT, GRANIT_VERTEX_FORMAT_FLOAT32X4,
                                static_cast<std::uint32_t>(offsetof(gpu_vertex, tangent)), 0},
        granit_vertex_attribute{
            GRANIT_PBR_VERTEX_LOCATION_UV0, GRANIT_VERTEX_FORMAT_FLOAT32X2,
            static_cast<std::uint32_t>(offsetof(gpu_vertex, texture_coordinate)), 0}};
    const granit_vertex_buffer_layout layout{sizeof(gpu_vertex), GRANIT_VERTEX_STEP_MODE_VERTEX,
                                             static_cast<std::uint32_t>(attributes.size()), 0,
                                             attributes.data()};
    if (granit_pbr_validate_vertex_layout(&layout, 1, GRANIT_PBR_TEXTURE_ALL) !=
        GRANIT_PBR_VERTEX_LAYOUT_VALID) {
      return granit::result::invalid_argument;
    }
    std::array<granit::vertex_attribute, 4> typed_attributes;
    for (std::size_t index = 0; index < attributes.size(); ++index) {
      typed_attributes[index] = {attributes[index].location,
                                 static_cast<granit::vertex_format>(attributes[index].format),
                                 attributes[index].offset};
    }
    const granit::vertex_buffer_layout typed_layout{
        sizeof(gpu_vertex), granit::vertex_step_mode::vertex, typed_attributes};
    for (auto& [rid, mirror] : mesh_mirrors_) {
      const auto* source = resources.get_mesh(rid);
      const granit::mesh_vertex_buffer vertex_buffer{geometry_vertices_.ref(), 0, typed_layout};
      granit::mesh_desc desc{};
      desc.vertex_buffers = {&vertex_buffer, 1};
      desc.index_buffer = geometry_indices_.ref();
      desc.index_format = granit::index_type::uint32;
      desc.index_count = mirror.index_count;
      desc.first_index = mirror.first_index;
      desc.vertex_offset = mirror.vertex_offset;
      result = mirror.mesh.initialize(renderer_, desc);
      if (result.failed())
        return result;
      mirror.source = source;
    }
    geometry_dirty_ = false;
    return granit::result::success;
  } catch (const std::bad_alloc&) {
    return granit::result::out_of_memory;
  } catch (...) {
    return granit::result::unknown;
  }
}

granit::result granit_render_service::ensure_default_textures() noexcept {
  if (default_white_srgb_.view.valid() && default_white_linear_.view.valid() &&
      default_normal_linear_.view.valid()) {
    return granit::result::success;
  }
  try {
    render_internal::texture_resource white_srgb{
        .width = 1,
        .height = 1,
        .format = GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,
        .color_space = GNEISS_TEXTURE_COLOR_SPACE_SRGB,
        .levels = {{.width = 1U,
                    .height = 1U,
                    .pixels = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
                               std::byte{0xff}}}},
        .manifest = {},
        .payload = {}};
    render_internal::texture_resource white_linear = white_srgb;
    white_linear.color_space = GNEISS_TEXTURE_COLOR_SPACE_LINEAR;
    render_internal::texture_resource normal_linear = white_linear;
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

granit::result granit_render_service::prepare_ui_draw_list(
    const render_internal::ui_draw_list& ui,
    const render_internal::render_resource_snapshot& resources, std::uint32_t width,
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
      auto found = texture_mirrors_.find(command.texture);
      if (found == texture_mirrors_.end()) {
        const auto* texture = resources.get_texture(command.texture);
        if (texture == nullptr) {
          return granit::result::invalid_handle;
        }
        texture_mirror mirror;
        result = create_texture_mirror(*texture, mirror, command.texture);
        if (result.failed()) {
          return result;
        }
        found = texture_mirrors_.emplace(command.texture, std::move(mirror)).first;
      }
      const auto first_index = static_cast<std::uint32_t>(indices.size());
      for (std::uint32_t item = 0; item < command.index_count; ++item) {
        indices.push_back(ui.indices()[command.first_index + item] + command.vertex_offset);
      }
      ranges.push_back({.first_index = first_index,
                        .index_count = command.index_count,
                        .state = {.texture = found->second.view.ref(),
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

void granit_render_service::release_invalid_textures(
    const render_internal::render_resource_snapshot& resources) noexcept {
  for (auto iterator = prepared_textures_.begin(); iterator != prepared_textures_.end();) {
    if (iterator->second.data.use_count() == 1) {
      iterator = prepared_textures_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  bool invalidated = false;
  for (auto iterator = texture_mirrors_.begin(); iterator != texture_mirrors_.end();) {
    if (resources.get_texture(iterator->first) != iterator->second.source) {
      iterator = texture_mirrors_.erase(iterator);
      invalidated = true;
    } else {
      ++iterator;
    }
  }
  // Material Instance 借用纹理视图；纹理投影变化时必须一并重建材质投影。
  if (invalidated) {
    material_mirrors_.clear();
  }
}

void granit_render_service::release_invalid_meshes(
    const render_internal::render_resource_snapshot& resources) noexcept {
  for (auto iterator = mesh_mirrors_.begin(); iterator != mesh_mirrors_.end();) {
    if (resources.get_mesh(iterator->first) != iterator->second.source) {
      iterator = mesh_mirrors_.erase(iterator);
      geometry_dirty_ = true;
    } else {
      ++iterator;
    }
  }
}

void granit_render_service::release_invalid_materials(
    const render_internal::render_resource_snapshot& resources) noexcept {
  for (auto iterator = material_mirrors_.begin(); iterator != material_mirrors_.end();) {
    if (resources.get_material(iterator->first) != iterator->second.source) {
      iterator = material_mirrors_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

gneiss_result granit_render_service::prepare_textures(std::vector<texture_data> data,
                                                      std::uint64_t& sequence) noexcept {
  try {
    if (data.empty()) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    return executor_.submit_command(
        [this, data = std::move(data)](const render_internal::render_command_reporter& reporter) {
          // 仅渲染线程创建候选；失败不影响已显示的镜像。
          std::vector<prepared_texture> pending;
          pending.reserve(data.size());
          for (const auto& item : data) {
            if (!item) {
              return GNEISS_ERROR_INVALID_ARGUMENT;
            }
            prepared_texture value{.data = item, .mirror = {}};
            const auto result = create_texture_mirror(*item, value.mirror);
            if (result.failed()) {
              return map_result(result);
            }
            pending.push_back(std::move(value));
            reporter.report(render_internal::render_command_stage::uploading, pending.size(),
                            data.size());
          }
          try {
            for (auto& value : pending) {
              const auto* key = value.data.get();
              prepared_textures_.insert_or_assign(key, std::move(value));
            }
          } catch (...) {
            for (const auto& item : data) {
              prepared_textures_.erase(item.get());
            }
            return GNEISS_ERROR_OUT_OF_MEMORY;
          }
          return GNEISS_SUCCESS;
        },
        sequence);

  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

bool granit_render_service::poll_texture_preparation(std::uint64_t sequence,
                                                     gneiss_result& result) noexcept {
  render_internal::render_command_completion completion;
  if (!executor_.try_take_command_completion(completion)) {
    return false;
  }
  latest_texture_upload_ms_ = completion.execution_ms;
  result = completion.sequence == sequence ? completion.status : GNEISS_ERROR_INTERNAL;
  return true;
}

gneiss_result granit_render_service::discard_prepared_textures(std::vector<texture_data> data,
                                                               std::uint64_t& sequence) noexcept {
  try {
    return executor_.submit_command(
        [this, data = std::move(data)](const render_internal::render_command_reporter&) {
          for (const auto& item : data) {
            prepared_textures_.erase(item.get());
          }
          return GNEISS_SUCCESS;
        },
        sequence);

  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result granit_render_service::initialize(const native_window_info& window,
                                                std::span<const std::byte> environment_asset,
                                                float environment_intensity,
                                                float environment_rotation_radians,
                                                log_internal::log_dispatcher* log) noexcept {
  log_ = log;
  const auto executor_result =
      executor_.initialize([this](render_internal::render_frame_packet& packet,
                                  render_internal::render_execution_result& output) noexcept {
        const auto result = execute_frame(packet, output);
        output.needs_recreate = packet.window.needs_recreate;
        return result;
      });
  if (executor_result != GNEISS_SUCCESS) {
    return executor_result;
  }

  gneiss_result initialize_result = GNEISS_ERROR_UNKNOWN;
  std::uint64_t sequence{};
  const auto submit_result = executor_.submit_command(
      [this, window, environment_asset, environment_intensity, environment_rotation_radians,
       &initialize_result](const render_internal::render_command_reporter& reporter) {
        reporter.report(render_internal::render_command_stage::uploading, 0U, 1U);
        initialize_result = initialize_gpu(window, environment_asset, environment_intensity,
                                           environment_rotation_radians);
        reporter.report(render_internal::render_command_stage::uploading, 1U, 1U);
        return initialize_result;
      },
      sequence);
  if (submit_result == GNEISS_SUCCESS) {
    static_cast<void>(executor_.flush());
  }

  render_internal::render_command_completion completion;
  const auto has_completion = executor_.try_take_command_completion(completion);
  if (submit_result != GNEISS_SUCCESS || !has_completion || completion.sequence != sequence ||
      completion.status != GNEISS_SUCCESS) {
    granit::renderer_resource_stats ignored{};
    std::uint64_t cleanup_sequence{};
    if (executor_.submit_command(
            [this, &ignored](const render_internal::render_command_reporter&) {
              return shutdown_gpu(ignored);
            },
            cleanup_sequence) == GNEISS_SUCCESS) {
      static_cast<void>(executor_.flush());
    }
    executor_.stop();
    return submit_result != GNEISS_SUCCESS
               ? submit_result
               : (has_completion ? completion.status : GNEISS_ERROR_INTERNAL);
  }
  return initialize_result;
}

gneiss_result granit_render_service::collect_completions() noexcept {
  render_internal::render_frame_completion completion;
  while (executor_.try_take_frame_completion(completion)) {
    if (recycled_frame_packets_.size() < 3U) {
      recycled_frame_packets_.push_back(std::move(completion.reusable_packet));
    }
    if (completion.policy == render_internal::render_frame_policy::required) {
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
    if (completion.status != GNEISS_SUCCESS) {
      return completion.status;
    }
  }
  return GNEISS_SUCCESS;
}

gneiss_result
granit_render_service::prepare_frame_packet_storage(render_internal::render_frame_packet& packet,
                                                    bool& out_should_prepare) noexcept {
  const auto completion_result = collect_completions();
  if (completion_result != GNEISS_SUCCESS) {
    return completion_result;
  }
  out_should_prepare = executor_.query_stats().pending_frames < 3U;
  if (!out_should_prepare) {
    executor_.record_skipped_frame_build();
    return GNEISS_SUCCESS;
  }
  if (!recycled_frame_packets_.empty()) {
    packet = std::move(recycled_frame_packets_.back());
    recycled_frame_packets_.pop_back();
  }
  return GNEISS_SUCCESS;
}

gneiss_result granit_render_service::submit(render_internal::render_frame_packet packet,
                                            render_internal::render_frame_policy policy,
                                            std::uint64_t* out_sequence) noexcept {
  const auto completion_result = collect_completions();
  if (completion_result != GNEISS_SUCCESS) {
    return completion_result;
  }
  packet.window.needs_recreate = packet.window.needs_recreate || pending_recreate_;
  pending_recreate_ = false;
  std::uint64_t sequence{};
  const auto result = executor_.submit_frame(std::move(packet), sequence, policy);
  if (result == GNEISS_SUCCESS && out_sequence != nullptr) {
    *out_sequence = sequence;
  }
  return result;
}

bool granit_render_service::try_take_required_completion(
    render_internal::render_frame_completion& completion) noexcept {
  if (collect_completions() != GNEISS_SUCCESS || required_frame_completions_.empty()) {
    return false;
  }
  completion = std::move(required_frame_completions_.front());
  required_frame_completions_.pop_front();
  return true;
}

render_internal::render_queue_stats
granit_render_service::query_performance_stats() const noexcept {
  return executor_.query_stats();
}

gneiss_result granit_render_service::finish_frames() noexcept {
  const auto flushed = executor_.flush();
  if (flushed != GNEISS_SUCCESS) {
    return flushed;
  }
  auto result = GNEISS_SUCCESS;
  for (auto completed = collect_completions(); completed != GNEISS_SUCCESS;
       completed = collect_completions()) {
    if (result == GNEISS_SUCCESS && completed != GNEISS_ERROR_NOT_READY) {
      result = completed;
    }
  }
  return result;
}

gneiss_result granit_render_service::shutdown(granit::renderer_resource_stats& stats) noexcept {
  if (!executor_.is_running()) {
    return GNEISS_ERROR_NOT_READY;
  }
  static_cast<void>(executor_.flush());
  const auto frame_result = collect_completions();

  std::uint64_t sequence{};
  const auto submit_result = executor_.submit_command(
      [this, &stats](const render_internal::render_command_reporter& reporter) {
        reporter.report(render_internal::render_command_stage::uploading, 0U, 1U);
        const auto result = shutdown_gpu(stats);
        reporter.report(render_internal::render_command_stage::uploading, 1U, 1U);
        return result;
      },
      sequence);
  if (submit_result == GNEISS_SUCCESS) {
    static_cast<void>(executor_.flush());
  }
  render_internal::render_command_completion completion;
  const auto has_completion = executor_.try_take_command_completion(completion);
  executor_.stop();
  recycled_frame_packets_.clear();
  required_frame_completions_.clear();
  if (frame_result != GNEISS_SUCCESS) {
    return frame_result;
  }
  if (submit_result != GNEISS_SUCCESS) {
    return submit_result;
  }
  if (!has_completion || completion.sequence != sequence) {
    return GNEISS_ERROR_INTERNAL;
  }
  return completion.status;
}

gneiss_result granit_render_service::initialize_gpu(const native_window_info& window,
                                                    std::span<const std::byte> environment_asset,
                                                    float environment_intensity,
                                                    float environment_rotation_radians) noexcept {
  auto result = renderer_.initialize({.application_name = "Gneiss",
                                      .enable_validation = false,
                                      .presentation = granit::presentation_mode::enabled});
  if (result.failed()) {
    return map_result(result);
  }

  switch (window.backend) {
  case native_window_backend::win32:
    result =
        surface_.initialize(renderer_, granit::surface_desc::win32(window.display, window.window));
    break;
  case native_window_backend::xcb:
    result = surface_.initialize(renderer_,
                                 granit::surface_desc::xcb(window.display, window.xcb_window));
    break;
  case native_window_backend::wayland:
    result = surface_.initialize(renderer_,
                                 granit::surface_desc::wayland(window.display, window.window));
    break;
  default:
    return GNEISS_ERROR_UNSUPPORTED;
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
    result = sampler_.initialize(renderer_, {.mag_filter = granit::filter::linear,
                                             .min_filter = granit::filter::linear,
                                             .mip_filter = granit::mipmap_filter::linear,
                                             .address_u = granit::address_mode::repeat,
                                             .address_v = granit::address_mode::repeat,
                                             .address_w = granit::address_mode::repeat,
                                             .max_lod = 0.0F});
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

gneiss_result granit_render_service::shutdown_gpu(granit::renderer_resource_stats& stats) noexcept {
  static_cast<void>(pipeline_.reset());
  static_cast<void>(environment_.reset());
  environment_info_ = {};
  material_mirrors_.clear();
  mesh_mirrors_.clear();
  texture_mirrors_.clear();
  prepared_textures_.clear();
  static_cast<void>(default_normal_linear_.view.reset());
  static_cast<void>(default_normal_linear_.texture.reset());
  static_cast<void>(default_white_linear_.view.reset());
  static_cast<void>(default_white_linear_.texture.reset());
  static_cast<void>(default_white_srgb_.view.reset());
  static_cast<void>(default_white_srgb_.texture.reset());
  static_cast<void>(ui_canvas_.destroy());
  static_cast<void>(debug_draw_.destroy());
  static_cast<void>(geometry_indices_.reset());
  static_cast<void>(geometry_vertices_.reset());
  static_cast<void>(ui_sampler_.reset());
  static_cast<void>(sampler_.reset());
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
  const auto result =
      query_result.failed()
          ? map_result(query_result)
          : (stats.total_live_count == 0U ? GNEISS_SUCCESS : GNEISS_ERROR_INVALID_STATE);
  static_cast<void>(renderer_.reset());
  return result;
}

// 单帧资源准备与提交必须保持 Granit 调用和失败回滚的线性顺序。
gneiss_result
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
granit_render_service::execute_frame(render_internal::render_frame_packet& packet,
                                     render_internal::render_execution_result& output) noexcept {
  output.gpu_timing_supported = gpu_timing_supported_;
  output.environment_asset_requested = environment_asset_requested_;
  output.environment_fallback = environment_fallback_;
  output.environment_intensity = environment_intensity_;
  output.environment_rotation_radians = environment_rotation_radians_;
  auto& window = packet.window;
  const auto& snapshot = packet.scene;
  const auto& resources = packet.resources;
  if (window.width == 0U || window.height == 0U)
    return GNEISS_SUCCESS;

  const auto resource_prepare_started = std::chrono::steady_clock::now();
  if (window.needs_recreate) {
    const auto recreate = swapchain_.recreate({.width = window.width, .height = window.height});
    if (recreate == granit::result::not_ready)
      return GNEISS_SUCCESS;
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
      if (!mesh_mirrors_.contains(instance.mesh)) {
        const auto* source = resources.get_mesh(instance.mesh);
        if (source == nullptr)
          return GNEISS_ERROR_INVALID_HANDLE;
        mesh_mirror mirror;
        mirror.source = source;
        mesh_mirrors_.emplace(instance.mesh, std::move(mirror));
        geometry_dirty_ = true;
      }
    }
    if (geometry_dirty_) {
      const auto rebuilt = rebuild_geometry_arena(resources);
      if (rebuilt.failed())
        return map_result(rebuilt);
    }

    for (const auto& instance : snapshot.instances) {
      const auto* material = resources.get_material(instance.material);
      if (material == nullptr)
        return GNEISS_ERROR_INVALID_HANDLE;
      granit_texture_view base_color = default_white_srgb_.view.native_handle();
      if (material->base_color_texture != GNEISS_NULL_TEXTURE) {
        const auto* texture = resources.get_texture(material->base_color_texture);
        if (texture == nullptr)
          return GNEISS_ERROR_INVALID_HANDLE;
        auto found = texture_mirrors_.find(material->base_color_texture);
        if (found == texture_mirrors_.end()) {
          texture_mirror mirror;
          const auto created =
              create_texture_mirror(*texture, mirror, material->base_color_texture);
          if (created.failed())
            return map_result(created);
          found = texture_mirrors_.emplace(material->base_color_texture, std::move(mirror)).first;
        }
        base_color = found->second.view.native_handle();
      }
      auto found = material_mirrors_.find(instance.material);
      if (found == material_mirrors_.end()) {
        material_mirror mirror;
        const auto created = create_material_mirror(*material, base_color, mirror);
        if (created.failed())
          return map_result(created);
        material_mirrors_.emplace(instance.material, std::move(mirror));
      }
    }
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
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
    return GNEISS_ERROR_OUT_OF_MEMORY;
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
        render_internal::matrix4 model;
        render_internal::matrix4 normal;
        if (!build_model_matrices(instance.transform, model, normal))
          return GNEISS_ERROR_INVALID_ARGUMENT;
        const auto payload = static_cast<std::uint64_t>(index) + 1U;
        const auto* mesh = resources.get_mesh(instance.mesh);
        renderables.push_back(
            {.model = to_granit_matrix(model),
             .normal_matrix = to_granit_matrix(normal),
             .bounds_center = {instance.transform.translation[0], instance.transform.translation[1],
                               instance.transform.translation[2]},
             .bounds_radius = mesh_bounds_radius(*mesh, instance.transform),
             .layer_mask = UINT64_MAX,
             .sort_key = instance.material,
             .payload = payload,
             .object_id = static_cast<std::uint32_t>(index + 1U),
             .reserved = 0});
        bindings.push_back({.payload = payload,
                            .mesh = mesh_mirrors_.at(instance.mesh).mesh.ref(),
                            .material = material_mirrors_.at(instance.material).material.ref()});
      }
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
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
          return GNEISS_ERROR_NOT_READY;
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
        return GNEISS_ERROR_INTERNAL;
      }
      packet.readback->pixels.resize(static_cast<std::size_t>(byte_count));
      std::uint64_t required{};
      captured = granit::copy_readback_result(operation, index, packet.readback->pixels, required);
      if (captured.failed()) {
        return map_result(captured);
      }
      packet.readback->width = window.width;
      packet.readback->height = window.height;
      return GNEISS_SUCCESS;
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
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
    return GNEISS_SUCCESS;
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

} // namespace gneiss::application_internal
