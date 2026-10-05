// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_resource_service.hpp"

#include "engine/asset/mesh_tangent.hpp"
#include "engine/asset/texture_container.hpp"

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

std::uint64_t saturated_add(std::uint64_t left, std::uint64_t right) noexcept {
  return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}
render_memory_usage measure(const mesh_resource& value) noexcept {
  return {.logical_bytes = value.data_bytes(), .cpu_data_bytes = value.data_bytes()};
}
render_memory_usage measure([[maybe_unused]] const material_resource& value) noexcept {
  return {.logical_bytes = sizeof(material_resource), .cpu_data_bytes = sizeof(material_resource)};
}
render_memory_usage measure(const texture_resource& value) noexcept {
  std::uint64_t bytes = value.payload.size();
  for (const auto& mip : value.levels) {
    bytes = saturated_add(bytes, mip.pixels.size());
  }
  auto cpu = saturated_add(bytes, value.manifest.size());
  if (value.payload_source) {
    bytes = value.payload_source->size();
    cpu = saturated_add(cpu, value.payload_source->metadata_bytes());
    if (const auto upload = value.upload_payload.lock()) {
      cpu = saturated_add(cpu, upload->size());
    }
  }
  return {.logical_bytes = saturated_add(bytes, value.manifest.size()), .cpu_data_bytes = cpu};
}
} // namespace

render_resource_service::render_resource_service(std::uint64_t memory_limit) noexcept
    : memory_limit_(memory_limit), domain_(allocate_domain()), meshes_(domain_),
      materials_(domain_), textures_(domain_) {}

render_memory_usage render_resource_service::memory_usage() const noexcept {
  render_memory_usage total;
  const auto collect = [&](auto& history) {
    std::erase_if(history, [](const auto& item) { return item.expired(); });
    for (const auto& item : history) {
      if (const auto resource = item.lock()) {
        const auto usage = measure(*resource);
        total.logical_bytes = saturated_add(total.logical_bytes, usage.logical_bytes);
        total.cpu_data_bytes = saturated_add(total.cpu_data_bytes, usage.cpu_data_bytes);
      }
    }
  };
  collect(mesh_history_);
  collect(material_history_);
  collect(texture_history_);
  return total;
}
std::uint64_t render_resource_service::available_memory_bytes() const noexcept {
  const auto usage = memory_usage();
  return memory_limit_ -
         std::min(memory_limit_, std::max(usage.logical_bytes, usage.cpu_data_bytes));
}
template <typename Resource>
std::shared_ptr<const Resource>
render_resource_service::track(std::shared_ptr<const Resource> resource,
                               std::vector<std::weak_ptr<const Resource>>& history) {
  if (std::ranges::any_of(history, [&](const auto& item) { return item.lock() == resource; })) {
    return resource;
  }
  const auto usage = measure(*resource);
  const auto current = memory_usage();
  if (usage.logical_bytes > memory_limit_ - std::min(memory_limit_, current.logical_bytes) ||
      usage.cpu_data_bytes > memory_limit_ - std::min(memory_limit_, current.cpu_data_bytes)) {
    throw std::bad_alloc{};
  }
  history.push_back(resource);
  return resource;
}

gneiss_result render_resource_service::create_mesh(const asset_internal::mesh_view& desc,
                                                   gneiss_mesh* out_mesh) noexcept {
  if (out_mesh == nullptr || !is_valid() || desc.vertices.size() < 3U) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto vertices = desc.vertices;
  const auto normals = desc.normals;
  const auto indices = desc.indices;
  const auto tangents = desc.tangents;
  const auto uv1 = desc.uv1;
  const auto colors = desc.colors;
  if ((!normals.empty() && normals.size() != vertices.size()) ||
      (!indices.empty() && (indices.size() < 3U || indices.size() % 3U != 0U)) ||
      (!tangents.empty() && (tangents.size() != vertices.size() || normals.empty())) ||
      (!uv1.empty() && uv1.size() != vertices.size()) ||
      (!colors.empty() && colors.size() != vertices.size())) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  if (!std::ranges::all_of(vertices,
                           [](const auto& vertex) {
                             return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                                    std::isfinite(vertex.z) && std::isfinite(vertex.u) &&
                                    std::isfinite(vertex.v);
                           }) ||
      !std::ranges::all_of(normals,
                           [](const auto& normal) {
                             const auto length =
                                 std::sqrt((normal.x * normal.x) + (normal.y * normal.y) +
                                           (normal.z * normal.z));
                             return std::isfinite(length) && std::abs(length - 1.0F) <= 1.0e-4F;
                           }) ||
      !std::ranges::all_of(indices, [&](const auto index) { return index < vertices.size(); }) ||
      !std::ranges::all_of(
          uv1, [](const auto& uv) { return std::isfinite(uv.u) && std::isfinite(uv.v); }) ||
      !std::ranges::all_of(colors, [](const auto& color) {
        return valid_color(color.r) && valid_color(color.g) && valid_color(color.b) &&
               valid_color(color.a);
      })) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (std::size_t index = 0; index < tangents.size(); ++index) {
    const auto& tangent = tangents[index];
    const auto& normal = normals[index];
    if (!asset_internal::valid_mesh_tangent({tangent.x, tangent.y, tangent.z, tangent.w},
                                            {normal.x, normal.y, normal.z})) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  *out_mesh = 0U;
  try {
    mesh_resource resource{.vertices = {vertices.begin(), vertices.end()},
                           .normals = {normals.begin(), normals.end()},
                           .indices = {indices.begin(), indices.end()},
                           .tangents = {tangents.begin(), tangents.end()},
                           .uv1 = {uv1.begin(), uv1.end()},
                           .colors = {colors.begin(), colors.end()}};
    return meshes_.create(
        core::resource_type::mesh,
        track(std::make_shared<const mesh_resource>(std::move(resource)), mesh_history_), out_mesh);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_resource_service::create_prepared_mesh(mesh_resource resource,
                                                            gneiss_mesh* output) noexcept {
  if (output == nullptr || !is_valid()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *output = 0U;
  try {
    return meshes_.create(
        core::resource_type::mesh,
        track(std::make_shared<const mesh_resource>(std::move(resource)), mesh_history_), output);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}
bool render_resource_service::replace_mesh(gneiss_mesh rid,
                                           std::shared_ptr<const mesh_resource> data) noexcept {
  auto* slot = meshes_.get(rid, core::resource_type::mesh);
  if (!slot || !data) {
    return false;
  }
  try {
    *slot = track(std::move(data), mesh_history_);
  } catch (...) {
    return false;
  }
  return true;
}
bool render_resource_service::replace_material(
    gneiss_material rid, std::shared_ptr<const material_resource> data) noexcept {
  auto* slot = materials_.get(rid, core::resource_type::material);
  if (!slot || !data) {
    return false;
  }
  try {
    *slot = track(std::move(data), material_history_);
  } catch (...) {
    return false;
  }
  return true;
}

gneiss_result render_resource_service::destroy_mesh(gneiss_mesh mesh) noexcept {
  return meshes_.destroy(mesh, core::resource_type::mesh);
}

gneiss_result render_resource_service::create_material(const material_resource& value,
                                                       gneiss_material* out_material) noexcept {
  if (out_material == nullptr || !is_valid() || !valid_color(value.red) ||
      !valid_color(value.green) || !valid_color(value.blue) || !valid_color(value.alpha) ||
      !valid_color(value.metallic) || !valid_color(value.roughness)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_material = 0U;
  if (!std::isfinite(value.normal_scale) || !valid_color(value.occlusion_strength) ||
      !std::ranges::all_of(value.emissive, valid_color) ||
      value.alpha_mode > GNEISS_MATERIAL_ALPHA_BLEND || value.double_sided > 1U ||
      !std::isfinite(value.alpha_cutoff) || value.alpha_cutoff < 0.0F) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  for (const auto texture : value.texture_handles()) {
    if (texture != GNEISS_NULL_TEXTURE && get_texture(texture) == nullptr) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  for (const auto& sample : value.sampling) {
    if (sample.uv_set > 1U || sample.mag_filter > GNEISS_TEXTURE_FILTER_LINEAR ||
        sample.min_filter > GNEISS_TEXTURE_FILTER_LINEAR ||
        sample.mip_filter > GNEISS_TEXTURE_MIP_LINEAR ||
        sample.address_u > GNEISS_TEXTURE_ADDRESS_MIRROR ||
        sample.address_v > GNEISS_TEXTURE_ADDRESS_MIRROR) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
  }
  try {
    auto resource = track(std::make_shared<const material_resource>(value), material_history_);
    return materials_.create(core::resource_type::material, std::move(resource), out_material);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_resource_service::destroy_material(gneiss_material material) noexcept {
  return materials_.destroy(material, core::resource_type::material);
}

gneiss_result render_resource_service::create_texture(const texture_view& desc,
                                                      gneiss_texture* out_texture) noexcept {
  if (out_texture == nullptr || !is_valid() || desc.format != GNEISS_TEXTURE_FORMAT_RGBA8_UNORM ||
      (desc.color_space != GNEISS_TEXTURE_COLOR_SPACE_LINEAR &&
       desc.color_space != GNEISS_TEXTURE_COLOR_SPACE_SRGB) ||
      desc.width == 0U || desc.height == 0U || desc.width > maximum_texture_dimension ||
      desc.height > maximum_texture_dimension || desc.pixels.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto row_bytes = static_cast<std::uint64_t>(desc.width) * 4U;
  const auto packed_size = row_bytes * desc.height;
  const auto required_size = (static_cast<std::uint64_t>(desc.row_stride_bytes) *
                              static_cast<std::uint64_t>(desc.height - 1U)) +
                             row_bytes;
  if (desc.row_stride_bytes < row_bytes || packed_size > maximum_texture_bytes ||
      required_size > desc.pixels.size()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    texture_resource resource{
        .width = desc.width,
        .height = desc.height,
        .format = desc.format,
        .color_space = desc.color_space,
        .levels = {{.width = desc.width, .height = desc.height, .pixels = {}}},
        .manifest = {},
        .payload = {}};
    resource.levels.front().pixels.resize(static_cast<std::size_t>(packed_size));
    for (std::uint32_t row = 0; row < desc.height; ++row) {
      std::memcpy(resource.levels.front().pixels.data() +
                      (static_cast<std::size_t>(row) * row_bytes),
                  desc.pixels.data() + (static_cast<std::size_t>(row) * desc.row_stride_bytes),
                  static_cast<std::size_t>(row_bytes));
    }
    return create_texture(std::move(resource), out_texture);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_resource_service::create_texture(texture_resource resource,
                                                      gneiss_texture* out_texture) noexcept {
  if (out_texture == nullptr || !is_valid() || resource.width == 0U || resource.height == 0U ||
      resource.format != GNEISS_TEXTURE_FORMAT_RGBA8_UNORM || resource.levels.empty() ||
      !resource.manifest.empty() || !resource.payload.empty() ||
      resource.levels.front().width != resource.width ||
      resource.levels.front().height != resource.height) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto width = resource.width;
  auto height = resource.height;
  std::uint64_t total_bytes{};
  for (const auto& level : resource.levels) {
    const auto level_bytes = static_cast<std::uint64_t>(width) * height * 4U;
    if (level.width != width || level.height != height || level.pixels.size() != level_bytes ||
        level_bytes > maximum_texture_bytes || total_bytes > maximum_texture_bytes - level_bytes) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    total_bytes += level_bytes;
    width = std::max(1U, width / 2U);
    height = std::max(1U, height / 2U);
  }
  *out_texture = 0U;
  try {
    return textures_.create(
        core::resource_type::texture,
        track(std::make_shared<const texture_resource>(std::move(resource)), texture_history_),
        out_texture);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result
render_resource_service::create_packaged_texture(texture_resource resource,
                                                 gneiss_texture* out_texture) noexcept {
  if (out_texture == nullptr || !is_valid() || resource.width == 0U || resource.height == 0U ||
      resource.format != GNEISS_TEXTURE_FORMAT_RGBA8_UNORM ||
      (resource.color_space != GNEISS_TEXTURE_COLOR_SPACE_LINEAR &&
       resource.color_space != GNEISS_TEXTURE_COLOR_SPACE_SRGB) ||
      !resource.levels.empty() || resource.manifest.empty() ||
      (resource.payload.empty() &&
       (!resource.payload_source || resource.upload_payload.expired()))) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *out_texture = 0U;
  try {
    return textures_.create(
        core::resource_type::texture,
        track(std::make_shared<const texture_resource>(std::move(resource)), texture_history_),
        out_texture);
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_resource_service::destroy_texture(gneiss_texture texture) noexcept {
  return textures_.destroy(texture, core::resource_type::texture);
}

bool render_resource_service::replace_texture(
    gneiss_texture texture, std::shared_ptr<const texture_resource> prepared) noexcept {
  auto* slot = textures_.get(texture, core::resource_type::texture);
  if (slot == nullptr || !prepared) {
    return false;
  }
  try {
    *slot = track(std::move(prepared), texture_history_);
  } catch (...) {
    return false;
  }
  return true;
}

const mesh_resource* render_resource_service::get_mesh(gneiss_mesh mesh) const noexcept {
  const auto* resource = meshes_.get(mesh, core::resource_type::mesh);
  return resource == nullptr ? nullptr : resource->get();
}

const material_resource*
render_resource_service::get_material(gneiss_material material) const noexcept {
  const auto* resource = materials_.get(material, core::resource_type::material);
  return resource == nullptr ? nullptr : resource->get();
}

const texture_resource*
render_resource_service::get_texture(gneiss_texture texture) const noexcept {
  const auto* resource = textures_.get(texture, core::resource_type::texture);
  return resource == nullptr ? nullptr : resource->get();
}

std::shared_ptr<const mesh_resource>
render_resource_service::share_mesh(gneiss_mesh mesh) const noexcept {
  const auto* resource = meshes_.get(mesh, core::resource_type::mesh);
  return resource == nullptr ? nullptr : *resource;
}

std::shared_ptr<const material_resource>
render_resource_service::share_material(gneiss_material material) const noexcept {
  const auto* resource = materials_.get(material, core::resource_type::material);
  return resource == nullptr ? nullptr : *resource;
}

std::shared_ptr<const texture_resource>
render_resource_service::share_texture(gneiss_texture texture) const noexcept {
  const auto* resource = textures_.get(texture, core::resource_type::texture);
  return resource == nullptr ? nullptr : *resource;
}

std::size_t render_resource_service::live_resource_count() const noexcept {
  return meshes_.live_count() + materials_.live_count() + textures_.live_count();
}

} // namespace gneiss::render_internal
