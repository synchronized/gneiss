// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_asset_loader.hpp"
#include "engine/core/result.hpp"
#include <atomic>

#include "engine/function/render/render_resource_service.hpp"
#include "engine/resource/loaders/asset_parsing.hpp"
#include "engine/resource/loaders/mesh_binary.hpp"
#include "engine/resource/loaders/texture_container.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include "engine/resource/vfs/virtual_file_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <span>
#include <vector>

namespace {

using namespace gneiss::engine::resource::asset_parsing;

// 仅在所属线程的发布边界构造资源值，随后绑定经过验证的纹理租约。
[[nodiscard]] gneiss::engine::render::material_resource
material_data(const gneiss::engine::resource::material_parameters& parameters) noexcept {
  gneiss::engine::render::material_resource material{
      .red = parameters.color[0],
      .green = parameters.color[1],
      .blue = parameters.color[2],
      .alpha = parameters.color[3],
      .base_color_texture = gneiss::engine::core::null_rid,
      .metallic = parameters.metallic,
      .roughness = parameters.roughness,
  };
  material.normal_scale = parameters.normal_scale;
  material.occlusion_strength = parameters.occlusion_strength;
  material.emissive = parameters.emissive;
  material.alpha_mode = parameters.alpha_mode;
  material.double_sided = parameters.double_sided;
  material.alpha_cutoff = parameters.alpha_cutoff;
  material.sampling = parameters.sampling;

  return material;
}

// 数组所有权在发布边界移动，弱上传引用由 Render 随后建立。
[[nodiscard]] gneiss::engine::render::texture_resource
texture_data(gneiss::engine::resource::prepared_texture_data prepared) noexcept {
  return {
      .width = prepared.width,
      .height = prepared.height,
      .format = prepared.format,
      .color_space = prepared.color_space,
      .levels = std::move(prepared.levels),
      .manifest = std::move(prepared.manifest),
      .payload = std::move(prepared.payload),
      .profile = prepared.profile,
      .selected_variant = prepared.selected_variant,
      .payload_source = std::move(prepared.payload_source),
      .upload_payload = {},
  };
}

constexpr std::uint32_t mesh_type = 1;
constexpr std::uint32_t material_type = 2;
constexpr std::uint32_t texture_type = 3;

struct mesh_asset final {
  mesh_asset(gneiss::engine::render::render_resource_service& owner,
             gneiss::engine::core::rid value) noexcept
      : resources(&owner), rid(value) {}
  mesh_asset(const mesh_asset&) = delete;
  mesh_asset& operator=(const mesh_asset&) = delete;
  gneiss::engine::render::render_resource_service* resources;
  gneiss::engine::core::rid rid;
  std::uint64_t update_sequence{};
  ~mesh_asset() {
    if (resources != nullptr && rid != gneiss::engine::core::null_rid) {
      (void)resources->destroy_mesh(rid);
    }
  }
};

struct material_asset final {
  material_asset(
      gneiss::engine::render::render_resource_service& owner, gneiss::engine::core::rid value,
      std::array<std::shared_ptr<const gneiss::engine::resource::resource_cache::entry>, 5>
          texture_dependencies = {}) noexcept
      : resources(&owner), rid(value), textures(std::move(texture_dependencies)) {}
  material_asset(const material_asset&) = delete;
  material_asset& operator=(const material_asset&) = delete;
  gneiss::engine::render::render_resource_service* resources;
  gneiss::engine::core::rid rid;
  std::uint64_t update_sequence{};
  std::array<std::shared_ptr<const gneiss::engine::resource::resource_cache::entry>, 5> textures;
  ~material_asset() {
    if (resources != nullptr && rid != gneiss::engine::core::null_rid) {
      (void)resources->destroy_material(rid);
    }
  }
};

struct texture_asset final {
  texture_asset(gneiss::engine::render::render_resource_service& owner,
                gneiss::engine::core::rid value) noexcept
      : resources(&owner), rid(value) {}
  texture_asset(const texture_asset&) = delete;
  texture_asset& operator=(const texture_asset&) = delete;
  gneiss::engine::render::render_resource_service* resources;
  gneiss::engine::core::rid rid;
  std::uint64_t update_sequence{};
  ~texture_asset() {
    if (resources != nullptr && rid != gneiss::engine::core::null_rid) {
      (void)resources->destroy_texture(rid);
    }
  }
};

gneiss::engine::core::result
append_texture_dependencies(std::vector<gneiss::engine::render::render_asset_reload>& queue,
                            const std::array<std::string, 5>& uris) {
  using gneiss::engine::render::render_asset_type;
  for (const auto& uri : uris) {
    if (uri.empty()) {
      continue;
    }
    const auto found =
        std::ranges::find_if(queue, [&](const auto& item) { return item.uri == uri; });
    if (found != queue.end()) {
      if (found->type != render_asset_type::texture) {
        return gneiss::engine::core::result::invalid_argument;
      }
      continue;
    }
    if (queue.size() == 256U) {
      return gneiss::engine::core::result::invalid_argument;
    }
    queue.push_back({.uri = uri, .type = render_asset_type::texture});
  }
  return gneiss::engine::core::result::success;
}

} // namespace

namespace gneiss::engine::render {

gneiss::engine::core::rid mesh_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return gneiss::engine::core::null_rid;
  }
  return std::static_pointer_cast<mesh_asset>(entry_->resource)->rid;
}

gneiss::engine::core::rid material_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return gneiss::engine::core::null_rid;
  }
  return std::static_pointer_cast<material_asset>(entry_->resource)->rid;
}

gneiss::engine::core::rid texture_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return gneiss::engine::core::null_rid;
  }
  return std::static_pointer_cast<texture_asset>(entry_->resource)->rid;
}

render_asset_loader::render_asset_loader(const engine::resource::virtual_file_system& file_system,
                                         engine::resource::resource_cache& cache,
                                         render_resource_service& resources) noexcept
    : file_system_(file_system), cache_(cache), resources_(resources) {}

std::uint64_t render_asset_lease::get() const noexcept {
  if (!entry_ || !entry_->resource) {
    return 0U;
  }
  switch (type()) {
  case render_asset_type::invalid:
    return 0U;
  case render_asset_type::mesh:
    return std::static_pointer_cast<mesh_asset>(entry_->resource)->rid;
  case render_asset_type::material:
    return std::static_pointer_cast<material_asset>(entry_->resource)->rid;
  case render_asset_type::texture:
    return std::static_pointer_cast<texture_asset>(entry_->resource)->rid;
  }
  return 0U;
}
bool render_asset_loader::owns(const render_asset_lease& lease) const noexcept {
  engine::resource::asset_identity identity;
  if (!lease.entry_ || !lease.entry_->resource ||
      file_system_.identify(lease.entry_->uri, lease.entry_->type, identity) !=
          gneiss::engine::core::result::success ||
      identity.source != lease.entry_->source) {
    return false;
  }
  switch (lease.type()) {
  case render_asset_type::mesh:
    return std::static_pointer_cast<mesh_asset>(lease.entry_->resource)->resources == &resources_ &&
           resources_.mesh_status(lease.get()) != gneiss::engine::core::result::invalid_handle;
  case render_asset_type::material:
    return std::static_pointer_cast<material_asset>(lease.entry_->resource)->resources ==
               &resources_ &&
           resources_.material_status(lease.get()) != gneiss::engine::core::result::invalid_handle;
  case render_asset_type::texture:
    return std::static_pointer_cast<texture_asset>(lease.entry_->resource)->resources ==
               &resources_ &&
           resources_.texture_status(lease.get()) != gneiss::engine::core::result::invalid_handle;
  default:
    return false;
  }
}

std::array<render_asset_lease, 5>
render_asset_loader::texture_dependencies(const render_asset_lease& material) noexcept {
  std::array<render_asset_lease, 5> result;
  if (material.type() == render_asset_type::material && material.entry_->resource) {
    const auto& dependencies =
        std::static_pointer_cast<material_asset>(material.entry_->resource)->textures;
    for (std::size_t slot = 0; slot < result.size(); ++slot) {
      result[slot].entry_ = dependencies[slot];
    }
  }
  return result;
}

render_asset_lease render_asset_loader::asset_lease(const mesh_asset_lease& lease) noexcept {
  render_asset_lease result;
  result.entry_ = lease.entry_;
  return result;
}
render_asset_lease render_asset_loader::asset_lease(const material_asset_lease& lease) noexcept {
  render_asset_lease result;
  result.entry_ = lease.entry_;
  return result;
}

mesh_asset_lease render_asset_loader::mesh_lease(const render_asset_lease& lease) noexcept {
  mesh_asset_lease result;
  if (lease.type() == render_asset_type::mesh) {
    result.entry_ = lease.entry_;
  }
  return result;
}
material_asset_lease render_asset_loader::material_lease(const render_asset_lease& lease) noexcept {
  material_asset_lease result;
  if (lease.type() == render_asset_type::material) {
    result.entry_ = lease.entry_;
  }
  return result;
}

gneiss::engine::core::result
render_asset_loader::acquire_cached(const render_asset_reload& source, render_asset_lease& output,
                                    texture_prepare_profile profile) const noexcept {
  output = {};
  try {
    const auto current = cache_.observe(source.uri).lock();
    if (!current) {
      return gneiss::engine::core::result::not_found;
    }
    if (current->type != static_cast<std::uint32_t>(source.type) ||
        current->state != engine::resource::resource_state::ready) {
      return gneiss::engine::core::result::invalid_argument;
    }
    render_asset_lease target;
    target.entry_ = current;
    if (!owns(target)) {
      return gneiss::engine::core::result::invalid_handle;
    }
    auto status = gneiss::engine::core::result::invalid_argument;
    switch (source.type) {
    case render_asset_type::texture:
      status = resources_.texture_status(target.get());
      break;
    case render_asset_type::mesh:
      status = resources_.mesh_status(target.get());
      break;
    case render_asset_type::material:
      status = resources_.material_status(target.get());
      break;
    case render_asset_type::invalid:
      break;
    }
    if (status != gneiss::engine::core::result::success) {
      return status;
    }
    const auto matches_profile = [&](const auto& entry) {
      const auto rid = std::static_pointer_cast<texture_asset>(entry->resource)->rid;
      const auto* texture = resources_.get_texture(rid);
      return texture &&
             (profile.generation == 0U || texture->manifest.empty() || texture->profile == profile);
    };
    if (source.type == render_asset_type::texture && !matches_profile(current)) {
      return gneiss::engine::core::result::not_found;
    }
    if (source.type == render_asset_type::material) {
      const auto material = std::static_pointer_cast<material_asset>(current->resource);
      for (const auto& texture : material->textures) {
        if (texture && !matches_profile(texture)) {
          return gneiss::engine::core::result::not_found;
        }
      }
    }
    output.entry_ = current;
    return gneiss::engine::core::result::success;
  } catch (...) {
    return gneiss::engine::core::result::out_of_memory;
  }
}

std::uint64_t render_asset_loader::next_update_sequence() noexcept {
  // 多个加载器可共享同一来源缓存；请求顺序不能在每个加载器中从头计数。
  static std::atomic_uint64_t sequence{};
  return sequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
}

std::uint64_t*
render_asset_loader::update_sequence(const render_asset_lease& lease) const noexcept {
  if (!owns(lease)) {
    return nullptr;
  }
  switch (lease.type()) {
  case render_asset_type::mesh:
    return &std::static_pointer_cast<mesh_asset>(lease.entry_->resource)->update_sequence;
  case render_asset_type::texture:
    return &std::static_pointer_cast<texture_asset>(lease.entry_->resource)->update_sequence;
  case render_asset_type::material:
    return &std::static_pointer_cast<material_asset>(lease.entry_->resource)->update_sequence;
  case render_asset_type::invalid:
    return nullptr;
  }
  return nullptr;
}
bool render_asset_loader::claim_update(const render_asset_lease& lease,
                                       std::uint64_t sequence) noexcept {
  auto* current = update_sequence(lease);
  if (current == nullptr || sequence == 0U || sequence < *current) {
    return false;
  }
  *current = sequence;
  return true;
}

gneiss::engine::core::result
render_asset_loader::reserve_asset(const render_asset_reload& source,
                                   render_asset_lease& output) noexcept {
  output = {};
  try {
    if (engine::resource::validate_uri(source.uri) != gneiss::engine::core::result::success) {
      return gneiss::engine::core::result::invalid_argument;
    }
    engine::resource::asset_identity identity;
    const auto identified =
        file_system_.identify(source.uri, static_cast<std::uint32_t>(source.type), identity);
    if (identified != gneiss::engine::core::result::success) {
      return identified;
    }
    render_asset_lease lease;
    lease.entry_ = cache_.observe(source.uri).lock();
    if (lease) {
      if (lease.type() != source.type || !owns(lease)) {
        return gneiss::engine::core::result::invalid_handle;
      }
      output = std::move(lease);
      return gneiss::engine::core::result::success;
    }
    std::shared_ptr<void> resource;
    auto result = gneiss::engine::core::result::invalid_argument;
    switch (source.type) {
    case render_asset_type::mesh: {
      auto asset = std::make_shared<mesh_asset>(resources_, 0U);
      result = resources_.reserve_mesh(&asset->rid);
      resource = std::move(asset);
      break;
    }
    case render_asset_type::texture: {
      auto asset = std::make_shared<texture_asset>(resources_, 0U);
      result = resources_.reserve_texture(&asset->rid);
      resource = std::move(asset);
      break;
    }
    case render_asset_type::material: {
      auto asset = std::make_shared<material_asset>(resources_, 0U);
      result = resources_.reserve_material(&asset->rid);
      resource = std::move(asset);
      break;
    }
    case render_asset_type::invalid:
      break;
    }
    if (result == gneiss::engine::core::result::success) {
      return cache_.acquire(
          source.uri, static_cast<std::uint32_t>(source.type),
          [resource = std::move(resource)](auto& value) {
            value = resource;
            return gneiss::engine::core::result::success;
          },
          output.entry_, identity.source);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

gneiss::engine::core::result
render_asset_loader::prepare_material_upload(prepared_render_asset& prepared,
                                             asset_upload& upload) {
  auto material = material_data(prepared.material);
  for (std::size_t slot = 0; slot < prepared.texture_uris.size(); ++slot) {
    const auto& uri = prepared.texture_uris[slot];
    if (uri.empty()) {
      continue;
    }
    render_asset_lease dependency;
    const auto reserved =
        reserve_asset({.uri = uri, .type = render_asset_type::texture}, dependency);
    if (reserved != gneiss::engine::core::result::success) {
      return reserved;
    }
    material.set_texture(slot, dependency.get());
    upload.dependencies[slot] = dependency.entry_;
    upload.data.dependency_textures[slot] = resources_.share_texture(dependency.get());
  }
  return resources_.prepare_material_data(material, upload.data.material);
}

gneiss::engine::core::result render_asset_loader::prepare_upload(prepared_render_asset prepared,
                                                                 asset_upload& output,
                                                                 const render_asset_lease& target,
                                                                 std::uint64_t sequence) noexcept
    try {
  output = {};
  asset_upload upload;
  upload.target = target;
  if (!upload.target) {
    const auto reserved = reserve_asset(prepared.source, upload.target);
    if (reserved != gneiss::engine::core::result::success) {
      return reserved;
    }
  }
  if (upload.target.type() != prepared.source.type || upload.target.uri() != prepared.source.uri ||
      !owns(upload.target)) {
    return gneiss::engine::core::result::invalid_argument;
  }
  for (const auto& input : prepared.sources) {
    engine::resource::asset_identity identity;
    if (file_system_.identify(input.identity.uri, input.identity.type, identity) !=
            gneiss::engine::core::result::success ||
        identity != input.identity) {
      return gneiss::engine::core::result::invalid_argument;
    }
  }
  upload.sequence = sequence == 0U ? next_update_sequence() : sequence;
  if (!claim_update(upload.target, upload.sequence)) {
    return gneiss::engine::core::result::invalid_state;
  }
  upload.data.bytes = prepared.bytes;
  gneiss::engine::core::result result{gneiss::engine::core::result::invalid_argument};
  switch (prepared.source.type) {
  case render_asset_type::mesh:
    result = resources_.prepare_mesh_data(
        std::make_shared<const mesh_resource>(std::move(prepared.mesh)), upload.data.mesh);
    break;
  case render_asset_type::texture: {
    auto texture = texture_data(std::move(prepared.texture));
    if (texture.payload_source) {
      if (texture.profile.generation == 0U ||
          texture.payload.size() != texture.payload_source->size()) {
        return gneiss::engine::core::result::invalid_argument;
      }
      upload.data.texture_payload =
          std::make_shared<const std::vector<std::byte>>(std::move(texture.payload));
      texture.upload_payload = upload.data.texture_payload;
    }
    result = resources_.prepare_texture_data(std::move(texture), upload.data.texture);
    break;
  }
  case render_asset_type::material:
    result = prepare_material_upload(prepared, upload);
    break;
  case render_asset_type::invalid:
    return gneiss::engine::core::result::invalid_argument;
  }
  if (result == gneiss::engine::core::result::success) {
    output = std::move(upload);
  }
  return result;
} catch (const std::bad_alloc&) {
  return gneiss::engine::core::result::out_of_memory;
} catch (...) {
  return gneiss::engine::core::result::internal;
}

gneiss::engine::core::result render_asset_loader::complete_upload(asset_upload& upload) noexcept {
  if (!owns(upload.target) || *update_sequence(upload.target) != upload.sequence) {
    return gneiss::engine::core::result::invalid_state;
  }
  const auto rid = upload.target.get();
  if (upload.data.mesh) {
    return resources_.replace_mesh(rid, upload.data.mesh)
               ? gneiss::engine::core::result::success
               : gneiss::engine::core::result::out_of_memory;
  }
  if (upload.data.texture) {
    return resources_.replace_texture(rid, upload.data.texture)
               ? gneiss::engine::core::result::success
               : gneiss::engine::core::result::out_of_memory;
  }
  if (!upload.data.material) {
    return gneiss::engine::core::result::invalid_argument;
  }
  if (!resources_.replace_material(rid, upload.data.material)) {
    return gneiss::engine::core::result::out_of_memory;
  }
  auto material = std::static_pointer_cast<material_asset>(upload.target.entry_->resource);
  material->textures = upload.dependencies;
  return gneiss::engine::core::result::success;
}

gneiss::engine::core::result
render_asset_loader::acquire_mesh(std::string_view uri, mesh_asset_lease& out_lease,
                                  asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  engine::resource::asset_identity identity;
  const auto identified = file_system_.identify(uri, mesh_type, identity);
  if (identified != gneiss::engine::core::result::success) {
    return identified;
  }
  const auto result = cache_.acquire(
      uri, mesh_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss::engine::core::result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != gneiss::engine::core::result::success) {
          fail(out_diagnostic, result, "", "无法通过 VFS 读取 Mesh");
          return result;
        }
        std::vector<gneiss::engine::resource::mesh_vertex> vertices;
        std::vector<gneiss::engine::resource::mesh_normal> normals;
        std::vector<std::uint32_t> indices;
        std::vector<gneiss::engine::resource::mesh_tangent> tangents;
        std::vector<gneiss::engine::resource::mesh_uv> uv1;
        std::vector<gneiss::engine::resource::mesh_color> colors;
        result = gneiss::engine::resource::is_mesh_binary(bytes)
                     ? parse_binary_mesh(bytes, vertices, normals, indices, tangents, uv1, colors,
                                         out_diagnostic)
                     : parse_mesh(bytes, vertices, normals, out_diagnostic);
        if (result != gneiss::engine::core::result::success) {
          return result;
        }
        const engine::resource::mesh_view desc{
            .vertices = vertices,
            .normals = normals,
            .indices = indices,
            .tangents = tangents,
            .uv1 = uv1,
            .colors = colors,
        };
        gneiss::engine::core::rid rid = gneiss::engine::core::null_rid;
        result = resources_.create_mesh(desc, &rid);
        if (result != gneiss::engine::core::result::success) {
          fail(out_diagnostic, result, "", "创建 Mesh RID 失败");
          return result;
        }
        try {
          output = std::make_shared<mesh_asset>(resources_, rid);
        } catch (...) {
          (void)resources_.destroy_mesh(rid);
          throw;
        }
        return gneiss::engine::core::result::success;
      },
      out_lease.entry_, identity.source);
  if (result == gneiss::engine::core::result::success) {
    render_asset_lease target;
    target.entry_ = out_lease.entry_;
    if (!owns(target)) {
      out_lease = {};
      return gneiss::engine::core::result::invalid_handle;
    }
    const auto status = resources_.mesh_status(out_lease.get());
    if (status != gneiss::engine::core::result::success) {
      out_lease = {};
      fail(out_diagnostic, status, "", "资产内容尚未就绪或句柄已失效");
      return status;
    }
  }
  if (result != gneiss::engine::core::result::success &&
      out_diagnostic.result == gneiss::engine::core::result::success) {
    fail(out_diagnostic, result, "", "获取 Mesh 资产失败");
  }
  return result;
}

gneiss::engine::core::result
render_asset_loader::acquire_material(std::string_view uri, material_asset_lease& out_lease,
                                      asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  engine::resource::asset_identity identity;
  const auto identified = file_system_.identify(uri, material_type, identity);
  if (identified != gneiss::engine::core::result::success) {
    return identified;
  }
  const auto result = cache_.acquire(
      uri, material_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss::engine::core::result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != gneiss::engine::core::result::success) {
          fail(out_diagnostic, result, "", "无法通过 VFS 读取 Material");
          return result;
        }
        gneiss::engine::resource::material_source source;
        result = parse_material(bytes, source, out_diagnostic);
        if (result != gneiss::engine::core::result::success) {
          return result;
        }
        auto material = material_data(source.parameters);

        std::array<std::shared_ptr<const engine::resource::resource_cache::entry>, 5> dependencies;
        constexpr std::array names{
            "base_color_texture", "metallic_roughness_texture", "normal_texture",
            "occlusion_texture",  "emissive_texture",
        };
        for (std::size_t slot = 0; slot < source.texture_uris.size(); ++slot) {
          if (source.texture_uris[slot].empty()) {
            continue;
          }
          texture_asset_lease texture;
          asset_diagnostic texture_diagnostic;
          result = acquire_texture(source.texture_uris[slot], texture, texture_diagnostic);
          if (result != gneiss::engine::core::result::success) {
            fail(out_diagnostic, result, std::string{"/"} + names[slot],
                 texture_diagnostic.message);
            return result;
          }
          material.set_texture(slot, texture.get());
          dependencies[slot] = texture.entry_;
        }
        gneiss::engine::core::rid rid = gneiss::engine::core::null_rid;
        result = resources_.create_material(material, &rid);
        if (result != gneiss::engine::core::result::success) {
          fail(out_diagnostic, result, "", "创建 Material RID 失败");
          return result;
        }
        try {
          output = std::make_shared<material_asset>(resources_, rid, std::move(dependencies));
        } catch (...) {
          (void)resources_.destroy_material(rid);
          throw;
        }
        return gneiss::engine::core::result::success;
      },
      out_lease.entry_, identity.source);
  if (result == gneiss::engine::core::result::success) {
    render_asset_lease target;
    target.entry_ = out_lease.entry_;
    if (!owns(target)) {
      out_lease = {};
      return gneiss::engine::core::result::invalid_handle;
    }
    const auto status = resources_.material_status(out_lease.get());
    if (status != gneiss::engine::core::result::success) {
      out_lease = {};
      fail(out_diagnostic, status, "", "资产内容尚未就绪或句柄已失效");
      return status;
    }
  }
  if (result != gneiss::engine::core::result::success &&
      out_diagnostic.result == gneiss::engine::core::result::success) {
    fail(out_diagnostic, result, "", "获取 Material 资产失败");
  }
  return result;
}

gneiss::engine::core::result
render_asset_loader::acquire_texture(std::string_view uri, texture_asset_lease& out_lease,
                                     asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  engine::resource::asset_identity identity;
  const auto identified = file_system_.identify(uri, texture_type, identity);
  if (identified != gneiss::engine::core::result::success) {
    return identified;
  }
  const auto result = cache_.acquire(
      uri, texture_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss::engine::core::result {
        engine::resource::prepared_texture_data prepared;
        auto result = prepare_texture(file_system_, uri, prepared, out_diagnostic);
        if (result != gneiss::engine::core::result::success) {
          return result;
        }
        gneiss::engine::core::rid rid = gneiss::engine::core::null_rid;
        auto texture = texture_data(std::move(prepared));
        result = texture.manifest.empty()
                     ? resources_.create_texture(std::move(texture), &rid)
                     : resources_.create_packaged_texture(std::move(texture), &rid);
        if (result != gneiss::engine::core::result::success) {
          fail(out_diagnostic, result, "", "创建 Texture RID 失败");
          return result;
        }
        try {
          output = std::make_shared<texture_asset>(resources_, rid);
        } catch (...) {
          (void)resources_.destroy_texture(rid);
          throw;
        }
        return gneiss::engine::core::result::success;
      },
      out_lease.entry_, identity.source);
  if (result == gneiss::engine::core::result::success) {
    render_asset_lease target;
    target.entry_ = out_lease.entry_;
    if (!owns(target)) {
      out_lease = {};
      return gneiss::engine::core::result::invalid_handle;
    }
    const auto status = resources_.texture_status(out_lease.get());
    if (status != gneiss::engine::core::result::success) {
      out_lease = {};
      fail(out_diagnostic, status, "", "资产内容尚未就绪或句柄已失效");
      return status;
    }
  }
  if (result != gneiss::engine::core::result::success &&
      out_diagnostic.result == gneiss::engine::core::result::success) {
    fail(out_diagnostic, result, "", "获取 Texture 资产失败");
  }
  return result;
}

gneiss::engine::core::result
render_asset_loader::reload_assets(std::span<const render_asset_reload> assets,
                                   asset_diagnostic& out_diagnostic) noexcept {
  out_diagnostic = {};
  if (assets.empty()) {
    return gneiss::engine::core::result::invalid_argument;
  }
  try {
    if (assets.size() > 256U) {
      return gneiss::engine::core::result::invalid_argument;
    }
    std::vector<render_asset_reload> queue(assets.begin(), assets.end());
    gneiss::engine::core::result summary{gneiss::engine::core::result::success};
    for (std::size_t index = 0; index < queue.size(); ++index) {
      const auto source = queue[index];
      render_asset_lease target;
      if (index >= assets.size() && acquire_cached(source, target, default_profile_) ==
                                        gneiss::engine::core::result::success) {
        continue;
      }
      auto updated = reserve_asset(source, target);
      if (updated != gneiss::engine::core::result::success) {
        summary = updated;
        continue;
      }
      engine::resource::asset_preparation preparation(
          file_system_, std::span(&source, 1U),
          {
              .material_bytes = sizeof(material_resource),
              .maximum_assets = 1U,
              .maximum_bytes = std::size_t{256U} * 1024U * 1024U,
              .deferred_reads = false,
              .include_dependencies = false,
          },
          default_profile_);
      prepared_render_batch batch;
      bool complete{};
      do {
        updated = preparation.advance(std::size_t{256U} * 1024U * 1024U, {}, batch, out_diagnostic,
                                      complete);
      } while (!complete && updated == gneiss::engine::core::result::success);
      if (updated == gneiss::engine::core::result::success) {
        auto& prepared = batch.assets.front();
        updated = append_texture_dependencies(queue, prepared.texture_uris);
        asset_upload upload;
        if (updated == gneiss::engine::core::result::success) {
          updated = prepare_upload(std::move(prepared), upload, target);
        }
        if (updated == gneiss::engine::core::result::success) {
          updated = complete_upload(upload);
        }
      }
      if (updated != gneiss::engine::core::result::success) {
        summary = updated;
      }
    }
    if (summary != gneiss::engine::core::result::success) {
      fail(out_diagnostic, summary, "", "部分资源同步更新失败");
    }
    return summary;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::core::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::core::result::internal;
  }
}

} // namespace gneiss::engine::render
