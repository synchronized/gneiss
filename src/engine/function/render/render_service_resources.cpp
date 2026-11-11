// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "engine/function/render/detail/service_impl.hpp"
#include "engine/function/render/render_service.hpp"

#include "engine/resource/mesh/mesh_data.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

namespace gneiss {
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

render::detail::resource_storage::resource_storage(std::uint64_t memory_limit)
    : memory_limit_(memory_limit), domain_(allocate_domain()), meshes_(domain_),
      materials_(domain_), textures_(domain_) {}
result<
    std::array<std::shared_ptr<const gneiss::render::detail::resource_identity<texture_data>>, 5>>
render::detail::resource_storage::material_dependencies(
    const material_data& value,
    const gneiss::render::detail::resource_identity<material_data>* previous) const {
  std::array<std::shared_ptr<const gneiss::render::detail::resource_identity<texture_data>>, 5>
      dependencies{};
  const auto handles = value.texture_handles();
  const auto previous_version = previous ? previous->snapshot() : nullptr;
  for (std::size_t index = 0; index < handles.size(); ++index) {
    if (handles[index] == null_rid)
      continue;
    const auto* texture = textures_.get(handles[index], resource_type::texture);
    if (texture) {
      dependencies[index] = texture->identity;
      continue;
    }
    // 外部 RID 已交还时，材质仍可保留或重排自己原有的纹理依赖。
    if (previous_version && previous_version->data) {
      const auto old_handles = previous_version->data->texture_handles();
      for (std::size_t old = 0; old < old_handles.size(); ++old)
        if (old_handles[old] == handles[index]) {
          dependencies[index] = previous_version->textures[old];
          break;
        }
    }
    if (!dependencies[index])
      return error::invalid_handle;
  }
  return dependencies;
}
result<rid> render_service::create_scene() { return impl_->scenes.create_scene(); }
result<void> render_service::submit(render::scene_commands&& update) {
  return impl_->scenes.submit(std::move(update));
}
result<void> render_service::destroy_scene(rid scene) { return impl_->scenes.destroy_scene(scene); }

bool render_service::is_valid() const noexcept { return impl_->resources.domain_ != 0U; }
void render_service::close_resources() noexcept {
  impl_->scenes.close();
  impl_->resources.meshes_.close();
  impl_->resources.materials_.close();
  impl_->resources.textures_.close();
  impl_->resources.domain_ = 0U;
}

result<rid> render_service::reserve_mesh() {
  return impl_->resources.meshes_.create(resource_type::mesh, std::shared_ptr<const mesh_data>{});
}
result<rid> render_service::reserve_texture() {
  return impl_->resources.textures_.create(resource_type::texture,
                                           std::shared_ptr<const texture_data>{});
}
gneiss::error render_service::mesh_status(rid mesh) const noexcept {
  const auto* slot = impl_->resources.meshes_.get(mesh, resource_type::mesh);
  return !slot ? gneiss::error::invalid_handle
               : (slot->identity->read() ? gneiss::error::success : gneiss::error::not_ready);
}
result<rid> render_service::reserve_material() {
  return impl_->resources.materials_.create(resource_type::material,
                                            std::shared_ptr<const material_data>{});
}
gneiss::error render_service::material_status(rid material) const noexcept {
  const auto* slot = impl_->resources.materials_.get(material, resource_type::material);
  if (slot == nullptr) {
    return gneiss::error::invalid_handle;
  }
  return slot->identity->read() ? gneiss::error::success : gneiss::error::not_ready;
}
gneiss::error render_service::texture_status(rid texture) const noexcept {
  const auto* slot = impl_->resources.textures_.get(texture, resource_type::texture);
  return !slot ? gneiss::error::invalid_handle
               : (slot->identity->read() ? gneiss::error::success : gneiss::error::not_ready);
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
  collect(impl_->resources.mesh_history_);
  collect(impl_->resources.material_history_);
  collect(impl_->resources.texture_history_);
  return total;
}
std::uint64_t render_service::available_memory_bytes() const noexcept {
  const auto usage = memory_usage();
  return impl_->resources.memory_limit_ -
         std::min(impl_->resources.memory_limit_,
                  std::max(usage.logical_bytes, usage.cpu_data_bytes));
}
template <typename Resource>
result<std::shared_ptr<const Resource>>
render_service::track(std::shared_ptr<const Resource> resource,
                      std::vector<std::weak_ptr<const Resource>>& history) {
  if (std::ranges::any_of(history, [&](const auto& item) { return item.lock() == resource; })) {
    return resource;
  }
  const auto usage = measure(*resource);
  const auto current = memory_usage();
  if (usage.logical_bytes > impl_->resources.memory_limit_ -
                                std::min(impl_->resources.memory_limit_, current.logical_bytes) ||
      usage.cpu_data_bytes > impl_->resources.memory_limit_ -
                                 std::min(impl_->resources.memory_limit_, current.cpu_data_bytes)) {
    return error::out_of_memory; // 显式预算拒绝，非进程堆耗尽。
  }
  history.push_back(resource);
  return resource;
}

namespace {
gneiss::error validate_mesh(const mesh_view& desc) noexcept {
  if (desc.vertices.size() < 3U) {
    return gneiss::error::invalid_argument;
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
    return gneiss::error::invalid_argument;
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
    return gneiss::error::invalid_argument;
  }
  for (std::size_t index = 0; index < tangents.size(); ++index) {
    const auto& tangent = tangents[index];
    const auto& normal = normals[index];
    if (!gneiss::valid_mesh_tangent(tangent, normal)) {
      return gneiss::error::invalid_argument;
    }
  }
  return gneiss::error::success;
}
} // namespace

result<rid> render_service::create_mesh(const mesh_view& desc) {
  auto data = prepare_mesh_data(desc);
  if (!data)
    return data.error();
  return impl_->resources.meshes_.create(resource_type::mesh, std::move(*data));
}

result<std::shared_ptr<const mesh_data>> render_service::prepare_mesh_data(const mesh_view& desc) {
  if (!is_valid() || desc.vertices.size() < 3U) {
    return gneiss::error::invalid_argument;
  }
  const auto validated = validate_mesh(desc);
  if (validated != gneiss::error::success) {
    return validated;
  }
  const auto vertices = desc.vertices;
  const auto normals = desc.normals;
  const auto indices = desc.indices;
  const auto tangents = desc.tangents;
  const auto uv1 = desc.uv1;
  const auto colors = desc.colors;
  {
    mesh_data resource{
        .vertices = {vertices.begin(), vertices.end()},
        .normals = {normals.begin(), normals.end()},
        .indices = {indices.begin(), indices.end()},
        .tangents = {tangents.begin(), tangents.end()},
        .uv1 = {uv1.begin(), uv1.end()},
        .colors = {colors.begin(), colors.end()},
    };
    return prepare_mesh_data(std::make_shared<const mesh_data>(std::move(resource)));
  }
}

result<rid> render_service::create_mesh(mesh_data resource) {
  if (!is_valid()) {
    return gneiss::error::invalid_argument;
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
  if (validated.failed())
    return validated;
  return create_prepared_mesh(std::move(resource));
}

result<rid> render_service::create_prepared_mesh(mesh_data resource) {
  if (!is_valid()) {
    return gneiss::error::invalid_argument;
  }
  {
    return create_prepared_mesh(std::make_shared<const mesh_data>(std::move(resource)));
  }
}
result<rid> render_service::create_prepared_mesh(std::shared_ptr<const mesh_data> resource) {
  if (!is_valid() || !resource) {
    return gneiss::error::invalid_argument;
  }
  {
    auto tracked = track(std::move(resource), impl_->resources.mesh_history_);
    if (!tracked)
      return tracked.error();
    return impl_->resources.meshes_.create(resource_type::mesh, std::move(*tracked));
  }
}
result<void> render_service::replace_mesh(rid rid, std::shared_ptr<const mesh_data> data) {
  auto* slot = impl_->resources.meshes_.get(rid, resource_type::mesh);
  if (!slot || !data) {
    return error::invalid_argument;
  }
  {
    auto tracked = track(std::move(data), impl_->resources.mesh_history_);
    if (!tracked)
      return tracked.error();
    auto identity = slot->identity;
    auto data = *tracked;
    if (auto status = dispatch_render_work([identity, data] { identity->consume(data); }); !status)
      return status.error();
    identity->accept(std::move(data));
  }
  return {};
}
result<void> render_service::replace_material(rid rid, std::shared_ptr<const material_data> data) {
  auto* slot = impl_->resources.materials_.get(rid, resource_type::material);
  if (!slot || !data)
    return error::invalid_argument;
  auto dependencies = impl_->resources.material_dependencies(*data, slot->identity.get());
  if (!dependencies)
    return dependencies.error();
  auto tracked = track(std::move(data), impl_->resources.material_history_);
  if (!tracked)
    return tracked.error();
  // 所属线程一次发布内容和依赖；失败时旧版本不变。
  using identity_type = gneiss::render::detail::resource_identity<material_data>;
  auto next = std::make_shared<const identity_type::version>(
      identity_type::version{std::move(*tracked), std::move(*dependencies)});
  auto identity = slot->identity;
  if (auto status = dispatch_render_work([identity, next] { identity->consume(next); }); !status)
    return status.error();
  identity->accept(std::move(next));
  return {};
}

gneiss::error render_service::destroy_mesh(rid mesh) noexcept {
  const auto status = impl_->resources.meshes_.destroy(mesh, resource_type::mesh);
  return status;
}

result<std::shared_ptr<const mesh_data>>
render_service::prepare_mesh_data(std::shared_ptr<const mesh_data> resource) {
  if (!is_valid() || !resource)
    return gneiss::error::invalid_argument;
  {
    return track(std::move(resource), impl_->resources.mesh_history_);
  }
}

result<std::shared_ptr<const material_data>>
render_service::prepare_material_data(const material_data& value) {
  if (!is_valid())
    return gneiss::error::invalid_argument;
  const auto validation = validate_material_parameters(value.parameters);
  if (validation.error.failed())
    return validation.error;
  for (const auto texture : value.texture_handles()) {
    if (texture != null_rid && texture_status(texture) == gneiss::error::invalid_handle) {
      return gneiss::error::invalid_argument;
    }
  }
  {
    return track(std::make_shared<const material_data>(value), impl_->resources.material_history_);
  }
}

result<rid> render_service::create_material(const material_data& value) {
  auto data = prepare_material_data(value);
  if (!data)
    return data.error();
  auto dependencies = impl_->resources.material_dependencies(value);
  if (!dependencies)
    return dependencies.error();
  auto identity = std::make_shared<gneiss::render::detail::resource_identity<material_data>>();
  identity->publish(std::move(*data), std::move(*dependencies));
  return impl_->resources.materials_.create(resource_type::material, std::move(identity));
}

gneiss::error render_service::destroy_material(rid material) noexcept {
  const auto status = impl_->resources.materials_.destroy(material, resource_type::material);
  return status;
}

result<rid> render_service::create_texture(const texture_view& desc) {
  auto data = prepare_texture_data(desc);
  if (!data)
    return data.error();
  return impl_->resources.textures_.create(resource_type::texture, std::move(*data));
}

result<std::shared_ptr<const texture_data>>
render_service::prepare_texture_data(const texture_view& desc) {
  if (!is_valid() || desc.format != texture_format::rgba8_unorm ||
      (desc.color_space != texture_color_space::linear &&
       desc.color_space != texture_color_space::srgb) ||
      desc.width == 0U || desc.height == 0U || desc.width > maximum_texture_dimension ||
      desc.height > maximum_texture_dimension || desc.pixels.empty()) {
    return gneiss::error::invalid_argument;
  }
  const auto row_bytes = static_cast<std::uint64_t>(desc.width) * 4U;
  const auto packed_size = row_bytes * desc.height;
  const auto required_size = (static_cast<std::uint64_t>(desc.row_stride_bytes) *
                              static_cast<std::uint64_t>(desc.height - 1U)) +
                             row_bytes;
  if (desc.row_stride_bytes < row_bytes || packed_size > maximum_texture_bytes ||
      required_size > desc.pixels.size()) {
    return gneiss::error::invalid_argument;
  }
  {
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
    return prepare_texture_data(std::move(resource));
  }
}

result<std::shared_ptr<const texture_data>>
render_service::prepare_texture_data(texture_data resource) {
  return prepare_texture_data(std::make_shared<const texture_data>(std::move(resource)));
}

result<std::shared_ptr<const texture_data>>
render_service::prepare_texture_data(std::shared_ptr<const texture_data> owner) {
  if (!owner)
    return gneiss::error::invalid_argument;
  const auto& resource = *owner;
  if (resource.manifest.empty()) {
    if (!is_valid() || resource.width == 0U || resource.height == 0U ||
        resource.format != texture_format::rgba8_unorm || resource.levels.empty() ||
        !resource.manifest.empty() || !resource.payload.empty() ||
        resource.levels.front().width != resource.width ||
        resource.levels.front().height != resource.height) {
      return gneiss::error::invalid_argument;
    }
    auto width = resource.width;
    auto height = resource.height;
    std::uint64_t total_bytes{};
    for (const auto& level : resource.levels) {
      const auto level_bytes = static_cast<std::uint64_t>(width) * height * 4U;
      if (level.width != width || level.height != height || level.pixels.size() != level_bytes ||
          level_bytes > maximum_texture_bytes ||
          total_bytes > maximum_texture_bytes - level_bytes) {
        return gneiss::error::invalid_argument;
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
      return gneiss::error::invalid_argument;
    }
  }
  {
    return track(std::move(owner), impl_->resources.texture_history_);
  }
}

result<rid> render_service::create_texture(texture_data resource) {
  if (!resource.manifest.empty())
    return gneiss::error::invalid_argument;
  auto data = prepare_texture_data(std::move(resource));
  if (!data)
    return data.error();
  return impl_->resources.textures_.create(resource_type::texture, std::move(*data));
}

result<rid> render_service::create_packaged_texture(texture_data resource) {
  if (resource.manifest.empty())
    return gneiss::error::invalid_argument;
  auto data = prepare_texture_data(std::move(resource));
  if (!data)
    return data.error();
  return impl_->resources.textures_.create(resource_type::texture, std::move(*data));
}

gneiss::error render_service::destroy_texture(rid texture) noexcept {
  const auto status = impl_->resources.textures_.destroy(texture, resource_type::texture);
  return status;
}

result<void> render_service::replace_texture(rid texture,
                                             std::shared_ptr<const texture_data> prepared) {
  auto* slot = impl_->resources.textures_.get(texture, resource_type::texture);
  if (slot == nullptr || !prepared) {
    return error::invalid_argument;
  }
  {
    auto tracked = track(std::move(prepared), impl_->resources.texture_history_);
    if (!tracked)
      return tracked.error();
    auto identity = slot->identity;
    auto data = *tracked;
    if (auto status = dispatch_render_work([identity, data] { identity->consume(data); }); !status)
      return status.error();
    identity->accept(std::move(data));
  }
  return {};
}

const mesh_data* render_service::get_mesh(rid mesh) const noexcept {
  const auto* resource = impl_->resources.meshes_.get(mesh, resource_type::mesh);
  return resource == nullptr ? nullptr : resource->identity->read().get();
}

const material_data* render_service::get_material(rid material) const noexcept {
  const auto* resource = impl_->resources.materials_.get(material, resource_type::material);
  return resource == nullptr ? nullptr : resource->identity->read().get();
}

const texture_data* render_service::get_texture(rid texture) const noexcept {
  const auto* resource = impl_->resources.textures_.get(texture, resource_type::texture);
  return resource == nullptr ? nullptr : resource->identity->read().get();
}

std::shared_ptr<const mesh_data> render_service::share_mesh(rid mesh) const noexcept {
  const auto* resource = impl_->resources.meshes_.get(mesh, resource_type::mesh);
  return resource == nullptr ? nullptr : resource->identity->read();
}

std::shared_ptr<const material_data> render_service::share_material(rid material) const noexcept {
  const auto* resource = impl_->resources.materials_.get(material, resource_type::material);
  return resource == nullptr ? nullptr : resource->identity->read();
}

std::shared_ptr<const texture_data> render_service::share_texture(rid texture) const noexcept {
  const auto* resource = impl_->resources.textures_.get(texture, resource_type::texture);
  return resource == nullptr ? nullptr : resource->identity->read();
}

std::size_t render_service::live_resource_count() const noexcept {
  return impl_->resources.meshes_.live_count() + impl_->resources.materials_.live_count() +
         impl_->resources.textures_.live_count();
}

} // namespace gneiss
