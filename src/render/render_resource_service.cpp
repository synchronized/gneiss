// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_resource_service.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <span>

namespace gneiss::render_internal {
namespace {

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
    : domain_(allocate_domain()), meshes_(domain_), materials_(domain_) {}

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

const mesh_resource* render_resource_service::get_mesh(gneiss_mesh mesh) const noexcept {
  return meshes_.get(mesh, core::resource_type::mesh);
}

const material_resource*
render_resource_service::get_material(gneiss_material material) const noexcept {
  return materials_.get(material, core::resource_type::material);
}

std::size_t render_resource_service::live_resource_count() const noexcept {
  return meshes_.live_count() + materials_.live_count();
}

} // namespace gneiss::render_internal
