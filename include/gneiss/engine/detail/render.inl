// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_DETAIL_RENDER_INL_
#define GNEISS_DETAIL_RENDER_INL_

#include <gneiss/engine/render.hpp>

#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace gneiss {

[[nodiscard]] constexpr gneiss_texture_sampling to_native(const texture_sampling& value) noexcept {
  return {
      .uv_set = value.uv_set,
      .mag_filter = static_cast<std::uint32_t>(value.mag_filter),
      .min_filter = static_cast<std::uint32_t>(value.min_filter),
      .mip_filter = static_cast<std::uint32_t>(value.mip_filter),
      .address_u = static_cast<std::uint32_t>(value.address_u),
      .address_v = static_cast<std::uint32_t>(value.address_v),
  };
}

[[nodiscard]] constexpr gneiss_texture_desc to_native(const texture_desc& value) noexcept {
  return {
      .struct_size = sizeof(gneiss_texture_desc),
      .format = static_cast<std::uint32_t>(value.format),
      .color_space = static_cast<std::uint32_t>(value.color_space),
      .width = value.width,
      .height = value.height,
      .row_stride_bytes = value.row_stride_bytes,
      .pixel_data_size = value.pixels.size(),
      .pixels = value.pixels.data(),
      .reserved = {},
  };
}

[[nodiscard]] constexpr gneiss_material_desc to_native(const material_desc& value) noexcept {
  gneiss_material_desc native = GNEISS_MATERIAL_DESC_INIT;
  native.red = value.red;
  native.green = value.green;
  native.blue = value.blue;
  native.alpha = value.alpha;
  native.metallic = value.metallic;
  native.roughness = value.roughness;
  native.normal_scale = value.normal_scale;
  native.occlusion_strength = value.occlusion_strength;
  native.double_sided = static_cast<std::uint32_t>(value.double_sided);
  native.alpha_cutoff = value.alpha_cutoff;
  native.base_color_texture = value.base_color_texture.get();
  native.metallic_roughness_texture = value.metallic_roughness_texture.get();
  native.normal_texture = value.normal_texture.get();
  native.occlusion_texture = value.occlusion_texture.get();
  native.emissive_texture = value.emissive_texture.get();
  native.alpha_mode = static_cast<std::uint32_t>(value.alpha_mode);
  for (std::size_t index = 0; index < value.emissive.size(); ++index) {
    native.emissive[index] = value.emissive[index];
  }
  for (std::size_t index = 0; index < value.sampling.size(); ++index) {
    native.sampling[index] = to_native(value.sampling[index]);
  }
  return native;
}

[[nodiscard]] constexpr gneiss_ui_vertex to_native(const ui_vertex& value) noexcept {
  return {
      .position = {value.position[0], value.position[1]},
      .uv = {value.uv[0], value.uv[1]},
      .color_rgba8 = value.color_rgba8,
  };
}

[[nodiscard]] constexpr gneiss_ui_draw_command to_native(const ui_draw_command& value) noexcept {
  return {
      .texture = value.texture.get(),
      .clip_min = {value.clip_min[0], value.clip_min[1]},
      .clip_max = {value.clip_max[0], value.clip_max[1]},
      .first_index = value.first_index,
      .index_count = value.index_count,
      .vertex_offset = value.vertex_offset,
      .reserved = 0,
  };
}

[[nodiscard]] constexpr gneiss_debug_line to_native(const debug_line& value) noexcept {
  return {
      .start = {value.start[0], value.start[1], value.start[2]},
      .end = {value.end[0], value.end[1], value.end[2]},
      .color_rgba8 = value.color_rgba8,
      .width = value.width,
      .depth_test = static_cast<std::uint8_t>(value.depth_test),
      .reserved = {},
  };
}

[[nodiscard]] constexpr gneiss_camera_desc to_native(const camera_desc& value) noexcept {
  return {
      .struct_size = sizeof(gneiss_camera_desc),
      .reserved = 0,
      .vertical_field_of_view_radians = value.vertical_field_of_view_radians,
      .near_plane = value.near_plane,
      .far_plane = value.far_plane,
  };
}

[[nodiscard]] constexpr camera_desc from_native(const gneiss_camera_desc& value) noexcept {
  return {
      .vertical_field_of_view_radians = value.vertical_field_of_view_radians,
      .near_plane = value.near_plane,
      .far_plane = value.far_plane,
  };
}

[[nodiscard]] constexpr gneiss_camera to_native(const camera& value) noexcept {
  return {
      .vertical_field_of_view_radians = value.vertical_field_of_view_radians,
      .near_plane = value.near_plane,
      .far_plane = value.far_plane,
      .is_primary = static_cast<std::uint8_t>(value.is_primary),
      .reserved = {},
  };
}

[[nodiscard]] constexpr camera from_native(const gneiss_camera& value) noexcept {
  return {
      .vertical_field_of_view_radians = value.vertical_field_of_view_radians,
      .near_plane = value.near_plane,
      .far_plane = value.far_plane,
      .is_primary = value.is_primary != 0,
  };
}

namespace detail {
/** 临时数组仅在同步调用内存在，禁止按相同布局强转元素指针。 */
template <typename Value> auto render_values(std::span<const Value> values) {
  using Native = decltype(to_native(std::declval<const Value&>()));
  std::vector<Native> native;
  native.reserve(values.size());
  for (const auto& value : values) {
    native.push_back(to_native(value));
  }
  return native;
}
template <typename Desc, typename Call>
result with_render_desc(const Desc& desc, Call&& call) noexcept {
  return from_native(call(to_native(desc)));
}
template <typename Call> result with_render_desc(const mesh_desc& desc, Call&& call) noexcept {
  constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
  if (desc.vertices.size() > limit || desc.normals.size() > limit || desc.indices.size() > limit ||
      desc.tangents.size() > limit || desc.uv1.size() > limit || desc.colors.size() > limit) {
    return result::invalid_argument;
  }
  try {
    const auto vertices = render_values(desc.vertices);
    const auto normals = render_values(desc.normals);
    const auto tangents = render_values(desc.tangents);
    const auto uv1 = render_values(desc.uv1);
    const auto colors = render_values(desc.colors);
    gneiss_mesh_desc native = GNEISS_MESH_DESC_INIT;
    native.vertices = vertices.data();
    native.vertex_count = static_cast<std::uint32_t>(vertices.size());
    native.normals = normals.data();
    native.normal_count = static_cast<std::uint32_t>(normals.size());
    native.tangents = tangents.data();
    native.tangent_count = static_cast<std::uint32_t>(tangents.size());
    native.uv1 = uv1.data();
    native.uv1_count = static_cast<std::uint32_t>(uv1.size());
    native.colors = colors.data();
    native.color_count = static_cast<std::uint32_t>(colors.size());
    native.indices = desc.indices.data();
    native.index_count = static_cast<std::uint32_t>(desc.indices.size());
    return from_native(call(native));
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}
template <typename Call>
result with_render_desc(const ui_draw_list_desc& desc, Call&& call) noexcept {
  constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
  if (desc.vertices.size() > limit || desc.indices.size() > limit || desc.commands.size() > limit) {
    return result::invalid_argument;
  }
  try {
    const auto vertices = render_values(desc.vertices);
    const auto commands = render_values(desc.commands);
    gneiss_ui_draw_list_desc native = GNEISS_UI_DRAW_LIST_DESC_INIT;
    native.display_width = desc.display_width;
    native.display_height = desc.display_height;
    native.framebuffer_scale_x = desc.framebuffer_scale_x;
    native.framebuffer_scale_y = desc.framebuffer_scale_y;
    native.vertices = vertices.data();
    native.vertex_count = static_cast<std::uint32_t>(vertices.size());
    native.indices = desc.indices.data();
    native.index_count = static_cast<std::uint32_t>(desc.indices.size());
    native.commands = commands.data();
    native.command_count = static_cast<std::uint32_t>(commands.size());
    return from_native(call(native));
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}
template <typename Call>
result with_render_desc(const debug_draw_list_desc& desc, Call&& call) noexcept {
  if (desc.lines.size() > std::numeric_limits<std::uint32_t>::max()) {
    return result::invalid_argument;
  }
  try {
    const auto lines = render_values(desc.lines);
    const gneiss_debug_draw_list_desc native{
        .struct_size = sizeof(gneiss_debug_draw_list_desc),
        .reserved = 0,
        .line_count = static_cast<std::uint32_t>(lines.size()),
        .reserved_2 = 0,
        .lines = lines.data(),
    };
    return from_native(call(native));
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (const std::length_error&) {
    return result::invalid_argument;
  }
}
} // namespace detail

namespace detail {
template <typename Id, typename Desc, auto Create, auto Destroy>
result render_resource_owner<Id, Desc, Create, Destroy>::create(
    gneiss_application application, const Desc& desc, render_resource_owner& output) noexcept {
  std::uint64_t handle{};
  const auto status = with_render_desc(
      desc, [&](const auto& native) { return Create(application, &native, &handle); });
  if (status.failed()) {
    return status;
  }
  const auto closed = output.reset();
  if (closed.failed()) {
    (void)Destroy(application, handle);
    return closed;
  }
  output.application_ = application;
  output.handle_ = handle;
  return result::success;
}

template <typename Id, typename Desc, auto Create, auto Destroy>
result render_resource_owner<Id, Desc, Create, Destroy>::reset() noexcept {
  if (handle_ == 0U) {
    return result::success;
  }
  const auto status = from_native(Destroy(application_, handle_));
  if (status.failed() && status != result::invalid_handle) {
    return status;
  }
  handle_ = 0U;
  application_ = GNEISS_NULL_APPLICATION;
  return result::success;
}

template <typename Id, typename Desc, auto Create, auto Destroy>
void render_resource_owner<Id, Desc, Create, Destroy>::reset_or_terminate() noexcept {
  if (reset().failed()) {
    std::terminate();
  }
}
} // namespace detail

} // namespace gneiss

#endif
