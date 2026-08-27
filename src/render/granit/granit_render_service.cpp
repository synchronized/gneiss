// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/granit/granit_render_service.h"
#include "render/granit/embedded_textured_shaders.h"
#include "render/normal_math.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace gneiss::application_internal {
namespace {

gneiss_result map_result(granit::result result) noexcept {
  switch (result) {
  case granit::result::success:
    return GNEISS_SUCCESS;
  case granit::result::invalid_argument:
    return GNEISS_ERROR_INVALID_ARGUMENT;
  case granit::result::invalid_handle:
    return GNEISS_ERROR_INVALID_HANDLE;
  case granit::result::out_of_memory:
    return GNEISS_ERROR_OUT_OF_MEMORY;
  case granit::result::unsupported:
  case granit::result::backend_unavailable:
    return GNEISS_ERROR_UNSUPPORTED;
  case granit::result::not_ready:
  case granit::result::out_of_date:
    return GNEISS_ERROR_NOT_READY;
  case granit::result::initialization_failed:
  case granit::result::incompatible_driver:
    return GNEISS_ERROR_INITIALIZATION_FAILED;
  default:
    return GNEISS_ERROR_DEPENDENCY_FAILED;
  }
}

granit::surface_type to_surface_type(native_window_backend backend) noexcept {
  switch (backend) {
  case native_window_backend::win32:
    return granit::surface_type::win32;
  case native_window_backend::xcb:
    return granit::surface_type::xcb;
  case native_window_backend::wayland:
    return granit::surface_type::wayland;
  default:
    return granit::surface_type::none;
  }
}

struct gpu_vertex {
  float x;
  float y;
  float z;
  float w;
  float u;
  float v;
  float normal_x;
  float normal_y;
  float normal_z;
  float is_lit;
};

struct draw_batch final {
  granit_bind_group group{GRANIT_NULL_HANDLE};
  std::uint32_t dynamic_offset{};
  std::uint32_t first_index{};
  std::uint32_t index_count{};
};

} // namespace

granit::result granit_render_service::initialize_pipeline(granit::texture_format format) noexcept {
  auto result = granit::result::success;
  if (!vertex_shader_.valid()) {
    result = vertex_shader_.initialize(
        renderer_.native_handle(),
        {.stage = granit::shader_stage::vertex, .code = std::as_bytes(std::span{shaders::vertex})});
  }
  if (granit::succeeded(result) && !fragment_shader_.valid()) {
    result = fragment_shader_.initialize(renderer_.native_handle(),
                                         {.stage = granit::shader_stage::fragment,
                                          .code = std::as_bytes(std::span{shaders::fragment})});
  }
  if (granit::succeeded(result) && !texture_layout_.valid()) {
    constexpr auto fragment = granit::shader_stage_flags::fragment;
    const std::array entries{
        granit::bind_group_layout_entry{.binding = 0,
                                        .type = granit::binding_type::sampled_texture,
                                        .array_count = 1,
                                        .visibility = fragment},
        granit::bind_group_layout_entry{.binding = 1,
                                        .type = granit::binding_type::sampler,
                                        .array_count = 1,
                                        .visibility = fragment}};
    result = texture_layout_.initialize(renderer_.native_handle(), entries);
  }
  if (granit::succeeded(result) && !object_layout_.valid()) {
    const std::array entries{
        granit::bind_group_layout_entry{.binding = 0,
                                        .type = granit::binding_type::dynamic_uniform_buffer,
                                        .array_count = 1,
                                        .visibility = granit::shader_stage_flags::vertex}};
    result = object_layout_.initialize(renderer_.native_handle(), entries);
  }
  if (granit::succeeded(result) && !pipeline_layout_.valid()) {
    const std::array layouts{texture_layout_.native_handle(), object_layout_.native_handle()};
    result = pipeline_layout_.initialize(renderer_.native_handle(), layouts);
  }
  if (granit::succeeded(result) && !sampler_.valid()) {
    result = sampler_.initialize(renderer_.native_handle(),
                                 {.mag_filter = granit::filter::linear,
                                  .min_filter = granit::filter::linear,
                                  .mip_filter = granit::mipmap_filter::nearest,
                                  .address_u = granit::address_mode::repeat,
                                  .address_v = granit::address_mode::repeat,
                                  .address_w = granit::address_mode::repeat,
                                  .max_lod = 0.0F});
  }
  const std::array attributes{granit::vertex_attribute{.location = 0,
                                                       .format = granit::vertex_format::float32x4,
                                                       .offset = offsetof(gpu_vertex, x)},
                              granit::vertex_attribute{.location = 1,
                                                       .format = granit::vertex_format::float32x2,
                                                       .offset = offsetof(gpu_vertex, u)},
                              granit::vertex_attribute{.location = 2,
                                                       .format = granit::vertex_format::float32x4,
                                                       .offset = offsetof(gpu_vertex, normal_x)}};
  const granit::vertex_buffer_layout vertex_layout{.stride = sizeof(gpu_vertex),
                                                   .step_mode = granit::vertex_step_mode::vertex,
                                                   .attributes = attributes};
  if (granit::succeeded(result)) {
    result = pipeline_.initialize(
        renderer_.native_handle(),
        {.layout = pipeline_layout_.native_handle(),
         .vertex_shader = vertex_shader_.native_handle(),
         .fragment_shader = fragment_shader_.native_handle(),
         .color_formats = std::span{&format, 1},
         .depth_stencil_format = granit::texture_format::d32_float,
         .vertex_buffers = std::span{&vertex_layout, 1},
         .primitive = {},
         .depth = granit::depth_state{.test_enabled = true,
                                      .write_enabled = true,
                                      .compare = granit::compare_operation::less},
         .color_blends = {},
         .depth_bias = std::nullopt});
  }
  if (granit::succeeded(result)) {
    swapchain_format_ = format;
  }
  return result;
}

granit::result granit_render_service::ensure_depth_target(std::uint32_t width,
                                                          std::uint32_t height) noexcept {
  if (depth_view_.valid() && depth_width_ == width && depth_height_ == height) {
    return granit::result::success;
  }
  static_cast<void>(depth_view_.reset());
  static_cast<void>(depth_texture_.reset());
  depth_width_ = 0;
  depth_height_ = 0;
  auto result = depth_texture_.initialize(renderer_.native_handle(),
                                          {.format = granit::texture_format::d32_float,
                                           .usage = granit::texture_usage::depth_stencil_attachment,
                                           .width = width,
                                           .height = height});
  if (granit::succeeded(result)) {
    result = depth_view_.initialize(renderer_.native_handle(), depth_texture_.native_handle());
  }
  if (granit::succeeded(result)) {
    depth_width_ = width;
    depth_height_ = height;
  } else {
    static_cast<void>(depth_view_.reset());
    static_cast<void>(depth_texture_.reset());
  }
  return result;
}

granit::result
granit_render_service::create_texture_mirror(const render_internal::texture_resource& source,
                                             texture_mirror& output) noexcept {
  const auto format = source.color_space == GNEISS_TEXTURE_COLOR_SPACE_SRGB
                          ? granit::texture_format::rgba8_srgb
                          : granit::texture_format::rgba8_unorm;
  auto result = output.texture.initialize(
      renderer_.native_handle(),
      {.dimension = granit::texture_dimension::two_dimensional,
       .format = format,
       .usage = granit::texture_usage::sampled | granit::texture_usage::transfer_destination,
       .location = granit::memory_location::device,
       .width = source.width,
       .height = source.height});
  if (granit::succeeded(result)) {
    result = output.texture.write(
        source.pixels,
        {.offset = 0, .bytes_per_row = source.width * 4U, .rows_per_image = source.height},
        {.width = source.width, .height = source.height});
  }
  if (granit::succeeded(result)) {
    result = output.view.initialize(renderer_.native_handle(), output.texture.native_handle(),
                                    {.format = format});
  }
  if (granit::succeeded(result)) {
    const std::array entries{
        granit::bind_group_entry{.binding = 0, .resource = output.view.native_handle()},
        granit::bind_group_entry{.binding = 1, .resource = sampler_.native_handle()}};
    result = output.group.initialize(renderer_.native_handle(), texture_layout_.native_handle(),
                                     entries);
  }
  if (granit::failed(result)) {
    static_cast<void>(output.group.reset());
    static_cast<void>(output.view.reset());
    static_cast<void>(output.texture.reset());
  }
  return result;
}

granit::result granit_render_service::ensure_default_texture() noexcept {
  if (default_texture_.group.valid()) {
    return granit::result::success;
  }
  try {
    render_internal::texture_resource white{
        .width = 1,
        .height = 1,
        .format = GNEISS_TEXTURE_FORMAT_RGBA8_UNORM,
        .color_space = GNEISS_TEXTURE_COLOR_SPACE_SRGB,
        .pixels = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}}};
    return create_texture_mirror(white, default_texture_);
  } catch (const std::bad_alloc&) {
    return granit::result::out_of_memory;
  } catch (...) {
    return granit::result::unknown;
  }
}

granit::result
granit_render_service::ensure_uniform_arena(uniform_frame& frame,
                                            std::span<const std::byte> data) noexcept {
  if (data.empty()) {
    return granit::result::success;
  }
  if (!frame.buffer.valid() || frame.capacity < data.size()) {
    static_cast<void>(frame.group.reset());
    static_cast<void>(frame.buffer.reset());
    frame.capacity = 0;
    auto result = frame.buffer.initialize(renderer_.native_handle(),
                                          {.size = data.size(),
                                           .usage = granit::buffer_usage::uniform,
                                           .location = granit::memory_location::upload});
    if (granit::failed(result)) {
      return result;
    }
    const std::array entries{granit::bind_group_entry{.binding = 0,
                                                      .resource = frame.buffer.native_handle(),
                                                      .offset = 0,
                                                      .size = sizeof(object_uniform)}};
    result =
        frame.group.initialize(renderer_.native_handle(), object_layout_.native_handle(), entries);
    if (granit::failed(result)) {
      static_cast<void>(frame.buffer.reset());
      return result;
    }
    frame.capacity = data.size();
  }
  return frame.buffer.write(0, data);
}

void granit_render_service::release_invalid_textures(
    const render_internal::render_resource_service& resources) noexcept {
  for (auto iterator = texture_mirrors_.begin(); iterator != texture_mirrors_.end();) {
    if (resources.get_texture(iterator->first) == nullptr) {
      iterator = texture_mirrors_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

gneiss_result granit_render_service::initialize(const native_window_info& window) noexcept {
  auto result = renderer_.initialize({.application_name = "Gneiss",
                                      .enable_validation = false,
                                      .surface_types = to_surface_type(window.backend)});
  if (granit::failed(result)) {
    return map_result(result);
  }
  granit::renderer_limits limits;
  result = renderer_.get_limits(limits);
  if (granit::failed(result) || limits.max_uniform_buffer_binding_size < sizeof(object_uniform) ||
      !calculate_uniform_stride(limits.uniform_buffer_offset_alignment, uniform_stride_)) {
    return granit::failed(result) ? map_result(result) : GNEISS_ERROR_UNSUPPORTED;
  }

  switch (window.backend) {
  case native_window_backend::win32:
    result = surface_.initialize_win32(renderer_.native_handle(),
                                       {.instance = window.display, .window = window.window});
    break;
  case native_window_backend::xcb:
    result = surface_.initialize_xcb(renderer_.native_handle(),
                                     {.connection = window.display, .window = window.xcb_window});
    break;
  case native_window_backend::wayland:
    result = surface_.initialize_wayland(renderer_.native_handle(),
                                         {.display = window.display, .surface = window.window});
    break;
  default:
    return GNEISS_ERROR_UNSUPPORTED;
  }
  if (granit::succeeded(result)) {
    result = swapchain_.initialize(renderer_.native_handle(), surface_.native_handle(),
                                   {.width = window.width, .height = window.height});
  }
  if (granit::succeeded(result)) {
    result = frame_context_.initialize(renderer_.native_handle());
  }
  granit::swapchain_info swapchain_info;
  if (granit::succeeded(result)) {
    result = swapchain_.query_info(swapchain_info);
  }
  if (granit::succeeded(result)) {
    result = initialize_pipeline(swapchain_info.format);
  }
  if (granit::succeeded(result)) {
    result = ensure_depth_target(window.width, window.height);
  }
  return map_result(result);
}

// 单帧资源准备与提交必须保持 Granit 调用和失败回滚的线性顺序。
gneiss_result
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
granit_render_service::render(native_window_info& window,
                              const world_internal::render_snapshot& snapshot,
                              const render_internal::render_resource_service& resources) noexcept {
  if (window.width == 0U || window.height == 0U) {
    return GNEISS_SUCCESS;
  }
  if (window.needs_recreate) {
    const auto recreate_result =
        swapchain_.recreate({.width = window.width, .height = window.height});
    if (recreate_result == granit::result::not_ready) {
      return GNEISS_SUCCESS;
    }
    if (granit::failed(recreate_result)) {
      return map_result(recreate_result);
    }
    granit::swapchain_info swapchain_info;
    const auto query_result = swapchain_.query_info(swapchain_info);
    if (granit::failed(query_result)) {
      return map_result(query_result);
    }
    if (swapchain_info.format != swapchain_format_) {
      static_cast<void>(pipeline_.reset());
      const auto pipeline_result = initialize_pipeline(swapchain_info.format);
      if (granit::failed(pipeline_result)) {
        return map_result(pipeline_result);
      }
    }
    const auto depth_result = ensure_depth_target(window.width, window.height);
    if (granit::failed(depth_result)) {
      return map_result(depth_result);
    }
    window.needs_recreate = false;
  }

  auto& vertex_buffers = frame_vertex_buffers_[frame_index_ % frame_vertex_buffers_.size()];
  auto& index_buffers = frame_index_buffers_[frame_index_ % frame_index_buffers_.size()];
  auto& uniform_frame = uniform_frames_[frame_index_ % uniform_frames_.size()];
  vertex_buffers.clear();
  index_buffers.clear();
  std::uint32_t draw_index_count = 0;
  std::vector<draw_batch> batches;
  std::vector<std::byte> uniform_data;
  release_invalid_textures(resources);
  if (snapshot.has_camera) {
    try {
      std::vector<gpu_vertex> vertices;
      std::vector<std::uint32_t> indices;
      if (snapshot.instances.size() > std::numeric_limits<std::uint32_t>::max() / uniform_stride_) {
        return GNEISS_ERROR_OUT_OF_MEMORY;
      }
      uniform_data.resize(snapshot.instances.size() * uniform_stride_);
      for (const auto& instance : snapshot.instances) {
        const auto* mesh = resources.get_mesh(instance.mesh);
        const auto* material = resources.get_material(instance.material);
        if (mesh == nullptr || material == nullptr) {
          return GNEISS_ERROR_INVALID_HANDLE;
        }
        granit_bind_group group = GRANIT_NULL_HANDLE;
        if (material->base_color_texture == GNEISS_NULL_TEXTURE) {
          const auto texture_result = ensure_default_texture();
          if (granit::failed(texture_result)) {
            return map_result(texture_result);
          }
          group = default_texture_.group.native_handle();
        } else {
          const auto* texture = resources.get_texture(material->base_color_texture);
          if (texture == nullptr) {
            return GNEISS_ERROR_INVALID_HANDLE;
          }
          auto found = texture_mirrors_.find(material->base_color_texture);
          if (found == texture_mirrors_.end()) {
            texture_mirror mirror;
            const auto texture_result = create_texture_mirror(*texture, mirror);
            if (granit::failed(texture_result)) {
              return map_result(texture_result);
            }
            found = texture_mirrors_.emplace(material->base_color_texture, std::move(mirror)).first;
          }
          group = found->second.group.native_handle();
        }
        if (vertices.size() > std::numeric_limits<std::uint32_t>::max() - mesh->vertices.size()) {
          return GNEISS_ERROR_OUT_OF_MEMORY;
        }
        const auto first_vertex = static_cast<std::uint32_t>(vertices.size());
        object_uniform object;
        if (!build_object_uniform(
                snapshot.camera.view, snapshot.camera.projection, instance.transform,
                {material->red, material->green, material->blue, material->alpha}, object)) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
        const auto object_offset = batches.size() * uniform_stride_;
        std::memcpy(uniform_data.data() + object_offset, &object, sizeof(object));
        vertices.reserve(vertices.size() + mesh->vertices.size());
        for (std::size_t vertex_index = 0; vertex_index < mesh->vertices.size(); ++vertex_index) {
          const auto& source = mesh->vertices[vertex_index];
          const auto normal =
              mesh->normals.empty() ? gneiss_mesh_normal{} : mesh->normals[vertex_index];
          vertices.push_back({.x = source.x,
                              .y = source.y,
                              .z = source.z,
                              .w = 1.0F,
                              .u = source.u,
                              .v = source.v,
                              .normal_x = normal.x,
                              .normal_y = normal.y,
                              .normal_z = normal.z,
                              .is_lit = mesh->normals.empty() ? 0.0F : 1.0F});
        }
        const auto source_index_count =
            mesh->indices.empty() ? mesh->vertices.size() : mesh->indices.size();
        if (indices.size() > std::numeric_limits<std::uint32_t>::max() - source_index_count) {
          return GNEISS_ERROR_OUT_OF_MEMORY;
        }
        const auto first_index = static_cast<std::uint32_t>(indices.size());
        indices.reserve(indices.size() + source_index_count);
        if (mesh->indices.empty()) {
          for (std::uint32_t index = 0; index < mesh->vertices.size(); ++index) {
            indices.push_back(first_vertex + index);
          }
        } else {
          for (const auto index : mesh->indices) {
            indices.push_back(first_vertex + index);
          }
        }
        batches.push_back({.group = group,
                           .dynamic_offset = static_cast<std::uint32_t>(object_offset),
                           .first_index = first_index,
                           .index_count = static_cast<std::uint32_t>(source_index_count)});
      }
      if (!vertices.empty()) {
        granit::buffer buffer;
        const auto buffer_result = buffer.initialize(
            renderer_.native_handle(),
            {.size = vertices.size() * sizeof(gpu_vertex), .usage = granit::buffer_usage::vertex},
            std::as_bytes(std::span{vertices}));
        if (granit::failed(buffer_result)) {
          return map_result(buffer_result);
        }
        vertex_buffers.push_back(std::move(buffer));
      }
      if (!indices.empty()) {
        granit::buffer buffer;
        const auto buffer_result = buffer.initialize(
            renderer_.native_handle(),
            {.size = indices.size() * sizeof(std::uint32_t), .usage = granit::buffer_usage::index},
            std::as_bytes(std::span{indices}));
        if (granit::failed(buffer_result)) {
          return map_result(buffer_result);
        }
        index_buffers.push_back(std::move(buffer));
        draw_index_count = static_cast<std::uint32_t>(indices.size());
      }
      const auto uniform_result = ensure_uniform_arena(uniform_frame, uniform_data);
      if (granit::failed(uniform_result)) {
        return map_result(uniform_result);
      }
    } catch (const std::bad_alloc&) {
      return GNEISS_ERROR_OUT_OF_MEMORY;
    } catch (...) {
      return GNEISS_ERROR_INTERNAL;
    }
  }

  granit::acquired_frame frame;
  auto result = swapchain_.acquire(frame);
  if (result == granit::result::out_of_date) {
    window.needs_recreate = true;
    return GNEISS_SUCCESS;
  }
  if (granit::failed(result)) {
    return map_result(result);
  }
  window.needs_recreate = window.needs_recreate || frame.needs_recreate;

  granit_texture texture = GRANIT_NULL_HANDLE;
  granit_texture_view view = GRANIT_NULL_HANDLE;
  result = swapchain_.backbuffer(frame.image_index, texture, view);
  granit::frame_recording recording;
  if (granit::succeeded(result)) {
    result = frame_context_.begin(frame, recording);
  }
  if (granit::succeeded(result)) {
    result = recording.recorder().bind_graphics_pipeline(pipeline_.native_handle());
    const granit::viewport viewport{.x = 0.0F,
                                    .y = 0.0F,
                                    .width = static_cast<float>(window.width),
                                    .height = static_cast<float>(window.height),
                                    .min_depth = 0.0F,
                                    .max_depth = 1.0F};
    const granit::scissor scissor{.x = 0, .y = 0, .width = window.width, .height = window.height};
    if (granit::succeeded(result)) {
      result = recording.recorder().set_viewports(0, std::span{&viewport, 1});
    }
    if (granit::succeeded(result)) {
      result = recording.recorder().set_scissors(0, std::span{&scissor, 1});
    }
    if (granit::succeeded(result) && !vertex_buffers.empty()) {
      const granit::vertex_buffer_binding binding{.buffer = vertex_buffers.front().native_handle(),
                                                  .offset = 0};
      result = recording.recorder().bind_vertex_buffers(0, std::span{&binding, 1});
    }
    if (granit::succeeded(result) && !index_buffers.empty()) {
      result = recording.recorder().bind_index_buffer(index_buffers.front().native_handle(), 0,
                                                      granit::index_type::uint32);
    }
    const granit::color_attachment_desc color{
        .view = view, .clear_value = {.red = 0.04F, .green = 0.12F, .blue = 0.22F, .alpha = 1.0F}};
    const granit::depth_stencil_attachment_desc depth{.view = depth_view_.native_handle(),
                                                      .clear_value = {.depth = 1.0F}};
    const granit::rendering_desc rendering{
        .color_attachments = std::span{&color, 1},
        .depth_stencil_attachment = &depth,
        .area = {.x = 0, .y = 0, .width = window.width, .height = window.height}};
    if (granit::succeeded(result)) {
      result = recording.recorder().begin_rendering(rendering);
    }
    if (granit::succeeded(result) && draw_index_count > 0U) {
      for (const auto& batch : batches) {
        const std::array groups{batch.group, uniform_frame.group.native_handle()};
        const std::array offsets{batch.dynamic_offset};
        result = recording.recorder().bind_graphics_groups(pipeline_layout_.native_handle(), 0,
                                                           groups, offsets);
        if (granit::failed(result)) {
          break;
        }
        result = recording.recorder().draw_indexed(batch.index_count, 1, batch.first_index);
        if (granit::failed(result)) {
          break;
        }
      }
    }
    if (granit::succeeded(result)) {
      result = recording.recorder().end_rendering();
    }
  }
  if (granit::succeeded(result)) {
    result = recording.submit();
  }
  if (granit::succeeded(result)) {
    result = swapchain_.present(frame);
  }
  window.needs_recreate = window.needs_recreate || frame.needs_recreate;
  if (result == granit::result::out_of_date) {
    window.needs_recreate = true;
    result = granit::result::success;
  }
  if (granit::failed(result)) {
    if (recording.valid()) {
      static_cast<void>(recording.abort());
    }
    if (frame.valid()) {
      static_cast<void>(swapchain_.cancel(frame));
    }
  }
  if (granit::succeeded(result)) {
    ++frame_index_;
  }
  return map_result(result);
}

} // namespace gneiss::application_internal
