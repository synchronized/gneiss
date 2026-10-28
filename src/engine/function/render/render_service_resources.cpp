// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/render/render_resource_storage.hpp"
#include "engine/function/render/render_service.hpp"

#include "engine/resource/mesh/mesh_data.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

namespace gneiss::engine {
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
render_memory_usage measure(const mesh_data& value) noexcept {
  return {.logical_bytes = value.data_bytes(), .cpu_data_bytes = value.data_bytes()};
}
render_memory_usage measure([[maybe_unused]] const material_data& value) noexcept {
  return {.logical_bytes = sizeof(material_data), .cpu_data_bytes = sizeof(material_data)};
}
render_memory_usage measure(const texture_data& value) noexcept {
  std::uint64_t bytes = value.payload.size();
  for (const auto& mip : value.levels) {
    bytes = saturated_add(bytes, mip.pixels.size());
  }
  auto cpu = saturated_add(bytes, value.manifest.size());
  return {.logical_bytes = saturated_add(bytes, value.manifest.size()), .cpu_data_bytes = cpu};
}
} // namespace

render_service::resource_storage::resource_storage(std::uint64_t memory_limit)
    : memory_limit_(memory_limit), domain_(allocate_domain()), meshes_(domain_),
      materials_(domain_), textures_(domain_) {}

bool render_service::is_valid() const noexcept { return storage_->domain_ != 0U; }
void render_service::close_resources() noexcept {
  storage_->meshes_.close();
  storage_->materials_.close();
  storage_->textures_.close();
  storage_->domain_ = 0U;
}

engine::error render_service::reserve_mesh(rid* output) noexcept {
  return storage_->meshes_.create(resource_type::mesh, std::shared_ptr<const mesh_data>{}, output);
}
engine::error render_service::reserve_texture(rid* output) noexcept {
  return storage_->textures_.create(resource_type::texture, std::shared_ptr<const texture_data>{},
                                    output);
}
engine::error render_service::mesh_status(rid mesh) const noexcept {
  const auto* slot = storage_->meshes_.get(mesh, resource_type::mesh);
  return !slot ? engine::error::invalid_handle
               : (slot->data ? engine::error::success : engine::error::not_ready);
}
engine::error render_service::reserve_material(rid* output) noexcept {
  return storage_->materials_.create(resource_type::material,
                                     std::shared_ptr<const material_data>{}, output);
}
engine::error render_service::material_status(rid material) const noexcept {
  const auto* slot = storage_->materials_.get(material, resource_type::material);
  if (slot == nullptr) {
    return engine::error::invalid_handle;
  }
  return slot->data ? engine::error::success : engine::error::not_ready;
}
engine::error render_service::texture_status(rid texture) const noexcept {
  const auto* slot = storage_->textures_.get(texture, resource_type::texture);
  return !slot ? engine::error::invalid_handle
               : (slot->data ? engine::error::success : engine::error::not_ready);
}

render_memory_usage render_service::memory_usage() const noexcept {
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
  collect(storage_->mesh_history_);
  collect(storage_->material_history_);
  collect(storage_->texture_history_);
  return total;
}
std::uint64_t render_service::available_memory_bytes() const noexcept {
  const auto usage = memory_usage();
  return storage_->memory_limit_ -
         std::min(storage_->memory_limit_, std::max(usage.logical_bytes, usage.cpu_data_bytes));
}
template <typename Resource>
std::shared_ptr<const Resource>
render_service::track(std::shared_ptr<const Resource> resource,
                      std::vector<std::weak_ptr<const Resource>>& history) {
  if (std::ranges::any_of(history, [&](const auto& item) { return item.lock() == resource; })) {
    return resource;
  }
  const auto usage = measure(*resource);
  const auto current = memory_usage();
  if (usage.logical_bytes >
          storage_->memory_limit_ - std::min(storage_->memory_limit_, current.logical_bytes) ||
      usage.cpu_data_bytes >
          storage_->memory_limit_ - std::min(storage_->memory_limit_, current.cpu_data_bytes)) {
    throw std::bad_alloc{};
  }
  history.push_back(resource);
  return resource;
}

namespace {
engine::error validate_mesh(const mesh_view& desc) noexcept {
  if (desc.vertices.size() < 3U) {
    return engine::error::invalid_argument;
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
    return engine::error::invalid_argument;
  }
  if (!std::ranges::all_of(vertices,
                           [](const auto& vertex) {
                             return std::isfinite(vertex.position.x) &&
                                    std::isfinite(vertex.position.y) &&
                                    std::isfinite(vertex.position.z) &&
                                    std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y);
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
          uv1, [](const auto& uv) { return std::isfinite(uv.x) && std::isfinite(uv.y); }) ||
      !std::ranges::all_of(colors, [](const auto& color) {
        return valid_color(color.r) && valid_color(color.g) && valid_color(color.b) &&
               valid_color(color.a);
      })) {
    return engine::error::invalid_argument;
  }
  for (std::size_t index = 0; index < tangents.size(); ++index) {
    const auto& tangent = tangents[index];
    const auto& normal = normals[index];
    if (!engine::valid_mesh_tangent(tangent, normal)) {
      return engine::error::invalid_argument;
    }
  }
  return engine::error::success;
}
} // namespace

engine::error render_service::create_mesh(const mesh_view& desc, rid* out_mesh) noexcept {
  if (out_mesh == nullptr || !is_valid() || desc.vertices.size() < 3U) {
    return engine::error::invalid_argument;
  }
  const auto validated = validate_mesh(desc);
  if (validated != engine::error::success) {
    return validated;
  }
  const auto vertices = desc.vertices;
  const auto normals = desc.normals;
  const auto indices = desc.indices;
  const auto tangents = desc.tangents;
  const auto uv1 = desc.uv1;
  const auto colors = desc.colors;
  *out_mesh = 0U;
  try {
    mesh_data resource{
        .vertices = {vertices.begin(), vertices.end()},
        .normals = {normals.begin(), normals.end()},
        .indices = {indices.begin(), indices.end()},
        .tangents = {tangents.begin(), tangents.end()},
        .uv1 = {uv1.begin(), uv1.end()},
        .colors = {colors.begin(), colors.end()},
    };
    return storage_->meshes_.create(
        resource_type::mesh,
        track(std::make_shared<const mesh_data>(std::move(resource)), storage_->mesh_history_),
        out_mesh);
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error render_service::create_mesh(mesh_data resource, rid* output) noexcept {
  if (output == nullptr || !is_valid()) {
    return engine::error::invalid_argument;
  }
  const mesh_view view{
      .vertices = resource.vertices,
      .normals = resource.normals,
      .indices = resource.indices,
      .tangents = resource.tangents,
      .uv1 = resource.uv1,
      .colors = resource.colors,
  };
  const auto validated = validate_mesh(view);
  return validated == engine::error::success ? create_prepared_mesh(std::move(resource), output)
                                             : validated;
}

engine::error render_service::create_prepared_mesh(mesh_data resource, rid* output) noexcept {
  if (output == nullptr || !is_valid()) {
    return engine::error::invalid_argument;
  }
  *output = 0U;
  try {
    return storage_->meshes_.create(
        resource_type::mesh,
        track(std::make_shared<const mesh_data>(std::move(resource)), storage_->mesh_history_),
        output);
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
engine::error render_service::create_prepared_mesh(std::shared_ptr<const mesh_data> resource,
                                                   rid* output) noexcept {
  if (output == nullptr || !is_valid() || !resource) {
    return engine::error::invalid_argument;
  }
  *output = 0U;
  try {
    return storage_->meshes_.create(resource_type::mesh,
                                    track(std::move(resource), storage_->mesh_history_), output);
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}
bool render_service::replace_mesh(rid rid, std::shared_ptr<const mesh_data> data) noexcept {
  auto* slot = storage_->meshes_.get(rid, resource_type::mesh);
  if (!slot || !data) {
    return false;
  }
  try {
    slot->data = track(std::move(data), storage_->mesh_history_);
  } catch (...) {
    return false;
  }
  return true;
}
bool render_service::replace_material(rid rid, std::shared_ptr<const material_data> data) noexcept {
  auto* slot = storage_->materials_.get(rid, resource_type::material);
  if (!slot || !data) {
    return false;
  }
  try {
    slot->data = track(std::move(data), storage_->material_history_);
  } catch (...) {
    return false;
  }
  return true;
}

engine::error render_service::destroy_mesh(rid mesh) noexcept {
  const auto status = storage_->meshes_.destroy(mesh, resource_type::mesh);
  return status;
}

engine::error render_service::prepare_mesh_data(std::shared_ptr<const mesh_data> resource,
                                                std::shared_ptr<const mesh_data>& output) noexcept {
  output.reset();
  if (!is_valid() || !resource)
    return engine::error::invalid_argument;
  try {
    output = track(std::move(resource), storage_->mesh_history_);
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error
render_service::prepare_material_data(const material_data& value,
                                      std::shared_ptr<const material_data>& output) noexcept {
  output.reset();
  const auto& parameters = value.parameters;
  if (!is_valid() || !std::ranges::all_of(math::components(parameters.color), valid_color) ||
      !valid_color(parameters.metallic) || !valid_color(parameters.roughness)) {
    return engine::error::invalid_argument;
  }
  if (!std::isfinite(parameters.normal_scale) || !valid_color(parameters.occlusion_strength) ||
      !std::ranges::all_of(math::components(parameters.emissive), valid_color) ||
      parameters.alpha_mode > material_alpha_mode::blend ||
      !std::isfinite(parameters.alpha_cutoff) || parameters.alpha_cutoff < 0.0F) {
    return engine::error::invalid_argument;
  }
  for (const auto texture : value.texture_handles()) {
    if (texture != null_rid && texture_status(texture) == engine::error::invalid_handle) {
      return engine::error::invalid_argument;
    }
  }
  for (const auto& sample : value.parameters.sampling) {
    if (!engine::valid_sampling(sample)) {
      return engine::error::invalid_argument;
    }
  }
  try {
    output = track(std::make_shared<const material_data>(value), storage_->material_history_);
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error render_service::create_material(const material_data& value, rid* output) noexcept {
  if (!output)
    return engine::error::invalid_argument;
  *output = 0U;
  std::shared_ptr<const material_data> data;
  const auto result = prepare_material_data(value, data);
  return result == engine::error::success
             ? storage_->materials_.create(resource_type::material, std::move(data), output)
             : result;
}

engine::error render_service::destroy_material(rid material) noexcept {
  const auto status = storage_->materials_.destroy(material, resource_type::material);
  return status;
}

engine::error render_service::create_texture(const texture_view& desc, rid* out_texture) noexcept {
  if (out_texture == nullptr || !is_valid() || desc.format != texture_format::rgba8_unorm ||
      (desc.color_space != texture_color_space::linear &&
       desc.color_space != texture_color_space::srgb) ||
      desc.width == 0U || desc.height == 0U || desc.width > maximum_texture_dimension ||
      desc.height > maximum_texture_dimension || desc.pixels.empty()) {
    return engine::error::invalid_argument;
  }
  const auto row_bytes = static_cast<std::uint64_t>(desc.width) * 4U;
  const auto packed_size = row_bytes * desc.height;
  const auto required_size = (static_cast<std::uint64_t>(desc.row_stride_bytes) *
                              static_cast<std::uint64_t>(desc.height - 1U)) +
                             row_bytes;
  if (desc.row_stride_bytes < row_bytes || packed_size > maximum_texture_bytes ||
      required_size > desc.pixels.size()) {
    return engine::error::invalid_argument;
  }
  try {
    texture_data resource{.width = desc.width,
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
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error
render_service::prepare_texture_data(texture_data resource,
                                     std::shared_ptr<const texture_data>& output) noexcept {
  output.reset();
  if (resource.manifest.empty()) {
    if (!is_valid() || resource.width == 0U || resource.height == 0U ||
        resource.format != texture_format::rgba8_unorm || resource.levels.empty() ||
        !resource.manifest.empty() || !resource.payload.empty() ||
        resource.levels.front().width != resource.width ||
        resource.levels.front().height != resource.height) {
      return engine::error::invalid_argument;
    }
    auto width = resource.width;
    auto height = resource.height;
    std::uint64_t total_bytes{};
    for (const auto& level : resource.levels) {
      const auto level_bytes = static_cast<std::uint64_t>(width) * height * 4U;
      if (level.width != width || level.height != height || level.pixels.size() != level_bytes ||
          level_bytes > maximum_texture_bytes ||
          total_bytes > maximum_texture_bytes - level_bytes) {
        return engine::error::invalid_argument;
      }
      total_bytes += level_bytes;
      width = std::max(1U, width / 2U);
      height = std::max(1U, height / 2U);
    }
  } else {
    if (!is_valid() || resource.width == 0U || resource.height == 0U ||
        resource.format != texture_format::rgba8_unorm ||
        (resource.color_space != texture_color_space::linear &&
         resource.color_space != texture_color_space::srgb) ||
        !resource.levels.empty() || resource.manifest.empty() || resource.payload.empty()) {
      return engine::error::invalid_argument;
    }
  }
  try {
    output = track(std::make_shared<const texture_data>(std::move(resource)),
                   storage_->texture_history_);
    return engine::error::success;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

engine::error render_service::create_texture(texture_data resource, rid* output) noexcept {
  if (!output || !resource.manifest.empty())
    return engine::error::invalid_argument;
  *output = 0U;
  std::shared_ptr<const texture_data> data;
  const auto result = prepare_texture_data(std::move(resource), data);
  return result == engine::error::success
             ? storage_->textures_.create(resource_type::texture, std::move(data), output)
             : result;
}

engine::error render_service::create_packaged_texture(texture_data resource, rid* output) noexcept {
  if (!output || resource.manifest.empty())
    return engine::error::invalid_argument;
  *output = 0U;
  std::shared_ptr<const texture_data> data;
  const auto result = prepare_texture_data(std::move(resource), data);
  return result == engine::error::success
             ? storage_->textures_.create(resource_type::texture, std::move(data), output)
             : result;
}

engine::error render_service::destroy_texture(rid texture) noexcept {
  const auto status = storage_->textures_.destroy(texture, resource_type::texture);
  return status;
}

bool render_service::replace_texture(rid texture,
                                     std::shared_ptr<const texture_data> prepared) noexcept {
  auto* slot = storage_->textures_.get(texture, resource_type::texture);
  if (slot == nullptr || !prepared) {
    return false;
  }
  try {
    slot->data = track(std::move(prepared), storage_->texture_history_);
  } catch (...) {
    return false;
  }
  return true;
}

const mesh_data* render_service::get_mesh(rid mesh) const noexcept {
  const auto* resource = storage_->meshes_.get(mesh, resource_type::mesh);
  return resource == nullptr ? nullptr : resource->data.get();
}

const material_data* render_service::get_material(rid material) const noexcept {
  const auto* resource = storage_->materials_.get(material, resource_type::material);
  return resource == nullptr ? nullptr : resource->data.get();
}

const texture_data* render_service::get_texture(rid texture) const noexcept {
  const auto* resource = storage_->textures_.get(texture, resource_type::texture);
  return resource == nullptr ? nullptr : resource->data.get();
}

std::shared_ptr<const mesh_data> render_service::share_mesh(rid mesh) const noexcept {
  const auto* resource = storage_->meshes_.get(mesh, resource_type::mesh);
  return resource == nullptr ? nullptr : resource->data;
}

std::shared_ptr<const material_data> render_service::share_material(rid material) const noexcept {
  const auto* resource = storage_->materials_.get(material, resource_type::material);
  return resource == nullptr ? nullptr : resource->data;
}

std::shared_ptr<const texture_data> render_service::share_texture(rid texture) const noexcept {
  const auto* resource = storage_->textures_.get(texture, resource_type::texture);
  return resource == nullptr ? nullptr : resource->data;
}

std::size_t render_service::live_resource_count() const noexcept {
  return storage_->meshes_.live_count() + storage_->materials_.live_count() +
         storage_->textures_.live_count();
}

} // namespace gneiss::engine
