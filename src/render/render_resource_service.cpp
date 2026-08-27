// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_resource_service.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

namespace gneiss::render_internal {
namespace {

constexpr std::uint32_t maximum_texture_dimension = 16384U;
constexpr std::uint64_t maximum_texture_bytes = UINT64_C(256) * 1024U * 1024U;

std::uint16_t allocate_domain() noexcept {
  static std::atomic_uint32_t next_domain{UINT32_C(16)};
  const auto value = next_domain.fetch_add(1U, std::memory_order_relaxed);
  return value <= std::numeric_limits<std::uint16_t>::max() ? static_cast<std::uint16_t>(value)
                                                            : UINT16_C(0);
}

bool valid_color(float value) noexcept {
  return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

} // namespace

render_resource_service::render_resource_service() noexcept
    : domain_(allocate_domain()), meshes_(domain_), materials_(domain_), textures_(domain_) {}

gneiss_result render_resource_service::create_mesh(const gneiss_mesh_desc& desc,
                                                   gneiss_mesh* out_mesh) noexcept {
  if (out_mesh == nullptr || !is_valid() || desc.struct_size < sizeof(gneiss_mesh_desc) ||
      desc.reserved != 0U || desc.vertex_count < 3U || desc.vertices == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto vertices = std::span{desc.vertices, desc.vertex_count};
  if (!std::ranges::all_of(vertices, [](const auto& vertex) {
        return std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z);
      })) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    mesh_resource resource{.vertices = {vertices.begin(), vertices.end()}};
    return meshes_.create(core::resource_type::mesh, std::move(resource), out_mesh);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_resource_service::destroy_mesh(gneiss_mesh mesh) noexcept {
  return meshes_.destroy(mesh, core::resource_type::mesh);
}

gneiss_result render_resource_service::create_material(const gneiss_material_desc& desc,
                                                       gneiss_material* out_material) noexcept {
  if (out_material == nullptr || !is_valid() || desc.struct_size < sizeof(gneiss_material_desc) ||
      desc.reserved != 0U || !valid_color(desc.red) || !valid_color(desc.green) ||
      !valid_color(desc.blue) || !valid_color(desc.alpha)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  return materials_.create(
      core::resource_type::material,
      material_resource{
          .red = desc.red, .green = desc.green, .blue = desc.blue, .alpha = desc.alpha},
      out_material);
}

gneiss_result render_resource_service::destroy_material(gneiss_material material) noexcept {
  return materials_.destroy(material, core::resource_type::material);
}

gneiss_result render_resource_service::create_texture(const gneiss_texture_desc& desc,
                                                      gneiss_texture* out_texture) noexcept {
  if (out_texture == nullptr || !is_valid() || desc.struct_size < sizeof(gneiss_texture_desc) ||
      desc.format != GNEISS_TEXTURE_FORMAT_RGBA8_UNORM ||
      (desc.color_space != GNEISS_TEXTURE_COLOR_SPACE_LINEAR &&
       desc.color_space != GNEISS_TEXTURE_COLOR_SPACE_SRGB) ||
      desc.width == 0U || desc.height == 0U || desc.width > maximum_texture_dimension ||
      desc.height > maximum_texture_dimension || desc.pixels == nullptr || desc.reserved[0] != 0U ||
      desc.reserved[1] != 0U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto row_bytes = static_cast<std::uint64_t>(desc.width) * 4U;
  const auto packed_size = row_bytes * desc.height;
  const auto required_size = static_cast<std::uint64_t>(desc.row_stride_bytes) *
                                 static_cast<std::uint64_t>(desc.height - 1U) +
                             row_bytes;
  if (desc.row_stride_bytes < row_bytes || packed_size > maximum_texture_bytes ||
      required_size > desc.pixel_data_size) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    texture_resource resource{.width = desc.width,
                              .height = desc.height,
                              .format = desc.format,
                              .color_space = desc.color_space,
                              .pixels = {}};
    resource.pixels.resize(static_cast<std::size_t>(packed_size));
    for (std::uint32_t row = 0; row < desc.height; ++row) {
      std::memcpy(resource.pixels.data() + (static_cast<std::size_t>(row) * row_bytes),
                  desc.pixels + (static_cast<std::size_t>(row) * desc.row_stride_bytes),
                  static_cast<std::size_t>(row_bytes));
    }
    return textures_.create(core::resource_type::texture, std::move(resource), out_texture);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_resource_service::destroy_texture(gneiss_texture texture) noexcept {
  return textures_.destroy(texture, core::resource_type::texture);
}

const mesh_resource* render_resource_service::get_mesh(gneiss_mesh mesh) const noexcept {
  return meshes_.get(mesh, core::resource_type::mesh);
}

const material_resource*
render_resource_service::get_material(gneiss_material material) const noexcept {
  return materials_.get(material, core::resource_type::material);
}

const texture_resource*
render_resource_service::get_texture(gneiss_texture texture) const noexcept {
  return textures_.get(texture, core::resource_type::texture);
}

std::size_t render_resource_service::live_resource_count() const noexcept {
  return meshes_.live_count() + materials_.live_count() + textures_.live_count();
}

} // namespace gneiss::render_internal
