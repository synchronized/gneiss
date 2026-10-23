// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_asset_loader.hpp"

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
material_for_publication(const gneiss::engine::resource::material_parameters& parameters) noexcept {
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
texture_for_publication(gneiss::engine::resource::prepared_texture_data prepared) noexcept {
  return {.width = prepared.width,
          .height = prepared.height,
          .format = prepared.format,
          .color_space = prepared.color_space,
          .levels = std::move(prepared.levels),
          .manifest = std::move(prepared.manifest),
          .payload = std::move(prepared.payload),
          .profile = prepared.profile,
          .selected_variant = prepared.selected_variant,
          .payload_source = std::move(prepared.payload_source),
          .upload_payload = {}};
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
  std::shared_ptr<const gneiss::engine::resource::material_document_lease> document;
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
  if (!lease.entry_ || !lease.entry_->resource) {
    return false;
  }
  switch (lease.type()) {
  case render_asset_type::mesh:
    return std::static_pointer_cast<mesh_asset>(lease.entry_->resource)->resources == &resources_ &&
           resources_.mesh_status(lease.get()) != GNEISS_ERROR_INVALID_HANDLE;
  case render_asset_type::material:
    return std::static_pointer_cast<material_asset>(lease.entry_->resource)->resources ==
               &resources_ &&
           resources_.material_status(lease.get()) != GNEISS_ERROR_INVALID_HANDLE;
  case render_asset_type::texture:
    return std::static_pointer_cast<texture_asset>(lease.entry_->resource)->resources ==
               &resources_ &&
           resources_.texture_status(lease.get()) != GNEISS_ERROR_INVALID_HANDLE;
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

engine::resource::resource_cache::selection_key
render_asset_loader::selection_key(const engine::resource::asset_identity& identity,
                                   texture_prepare_profile profile) {
  auto configuration = std::string{"render-prepare-v1:"} + std::to_string(profile.generation) + ":";
  for (const auto supported : profile.sampled_transfer_formats) {
    configuration.push_back(supported ? '1' : '0');
  }
  return {.identity = identity, .configuration = std::move(configuration)};
}

gneiss_result
render_asset_loader::acquire_selected(const engine::resource::asset_identity& identity,
                                      texture_prepare_profile profile,
                                      render_asset_lease& output) const noexcept {
  output = {};
  try {
    render_asset_lease selected;
    selected.entry_ = cache_.observe_selected(selection_key(identity, profile)).lock();
    if (!selected.entry_ || selected.entry_->state != engine::resource::resource_state::ready) {
      return GNEISS_ERROR_NOT_READY;
    }
    if (!owns(selected)) {
      return GNEISS_ERROR_INVALID_HANDLE;
    }
    output = std::move(selected);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::acquire_selected(const render_asset_reload& source,
                                                    texture_prepare_profile profile,
                                                    render_asset_lease& output) const noexcept {
  output = {};
  engine::resource::asset_identity identity;
  const auto result =
      file_system_.identify(source.uri, static_cast<std::uint32_t>(source.type), identity);
  return result == GNEISS_SUCCESS ? acquire_selected(identity, profile, output) : result;
}

gneiss_result render_asset_loader::acquire_cached(const render_asset_reload& source,
                                                  render_asset_lease& output,
                                                  texture_prepare_profile profile) const noexcept {
  output = {};
  try {
    const auto current = cache_.observe(source.uri).lock();
    if (!current) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (current->type != static_cast<std::uint32_t>(source.type) ||
        current->state != engine::resource::resource_state::ready) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    render_asset_lease target;
    target.entry_ = current;
    auto status = GNEISS_ERROR_INVALID_ARGUMENT;
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
    if (status != GNEISS_SUCCESS) {
      return status;
    }
    const auto matches_profile = [&](const auto& entry) {
      const auto rid = std::static_pointer_cast<texture_asset>(entry->resource)->rid;
      const auto* texture = resources_.get_texture(rid);
      return texture &&
             (profile.generation == 0U || texture->manifest.empty() || texture->profile == profile);
    };
    if (source.type == render_asset_type::texture && !matches_profile(current)) {
      return GNEISS_ERROR_NOT_FOUND;
    }
    if (source.type == render_asset_type::material) {
      const auto material = std::static_pointer_cast<material_asset>(current->resource);
      for (const auto& texture : material->textures) {
        if (texture && !matches_profile(texture)) {
          return GNEISS_ERROR_NOT_FOUND;
        }
      }
    }
    output.entry_ = current;
    return GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
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

gneiss_result render_asset_loader::reserve_asset(const render_asset_reload& source,
                                                 render_asset_lease& output) noexcept {
  output = {};
  try {
    if (engine::resource::validate_uri(source.uri) != GNEISS_SUCCESS) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    render_asset_lease lease;
    lease.entry_ = cache_.observe(source.uri).lock();
    if (lease) {
      if (lease.type() != source.type || !owns(lease)) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
      output = std::move(lease);
      return GNEISS_SUCCESS;
    }
    std::shared_ptr<void> resource;
    auto result = GNEISS_ERROR_INVALID_ARGUMENT;
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
    if (result == GNEISS_SUCCESS) {
      return cache_.acquire(
          source.uri, static_cast<std::uint32_t>(source.type),
          [resource = std::move(resource)](auto& value) {
            value = resource;
            return GNEISS_SUCCESS;
          },
          output.entry_);
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::stage_asset(prepared_render_asset prepared,
                                               std::span<const asset_candidate> staged,
                                               asset_candidate& output, publication_mode mode,
                                               const render_asset_lease& reserved,
                                               std::uint64_t sequence) noexcept {
  output = {};
  try {
    asset_candidate candidate;
    candidate.mode = mode;
    candidate.source = prepared.source;
    candidate.bytes = prepared.bytes;
    const bool fixed = mode == publication_mode::fixed_version;
    render_asset_lease target = reserved;
    if (!fixed && !target) {
      const auto result = reserve_asset(prepared.source, target);
      if (result != GNEISS_SUCCESS) {
        return result;
      }
    }
    if (reserved && (fixed || reserved.type() != prepared.source.type ||
                     reserved.uri() != prepared.source.uri || !owns(reserved))) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (!fixed) {
      candidate.update_sequence = sequence == 0U ? next_update_sequence() : sequence;
      if (!claim_update(target, candidate.update_sequence)) {
        return GNEISS_ERROR_INVALID_STATE;
      }
    }
    if (fixed) {
      candidate.version = std::move(prepared.version);
      if (!candidate.version.identity.source || candidate.version.version.empty() ||
          candidate.version.identity.uri != prepared.source.uri ||
          candidate.version.identity.type != static_cast<std::uint32_t>(prepared.source.type)) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
    }
    const auto current = fixed ? cache_.observe(candidate.version).lock()
                               : cache_.observe(prepared.source.uri).lock();
    if (target && current != target.entry_) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    if (current && (current->type != static_cast<std::uint32_t>(prepared.source.type) ||
                    current->state != engine::resource::resource_state::ready)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    candidate.existed = static_cast<bool>(current);
    candidate.expected = current;
    if (fixed && current) {
      candidate.lease.entry_ = current;
      const auto existing = candidate.lease.get();
      switch (prepared.source.type) {
      case render_asset_type::mesh:
        candidate.mesh = resources_.share_mesh(existing);
        break;
      case render_asset_type::texture:
        candidate.texture = resources_.share_texture(existing);
        break;
      case render_asset_type::material:
        candidate.material = resources_.share_material(existing);
        candidate.dependencies =
            std::static_pointer_cast<material_asset>(current->resource)->textures;
        break;
      default:
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      if (!candidate.mesh && !candidate.texture && !candidate.material) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
      output = std::move(candidate);
      return GNEISS_SUCCESS;
    }
    std::shared_ptr<void> owned;
    std::uint64_t rid{};
    auto created = GNEISS_ERROR_INVALID_ARGUMENT;
    if (prepared.source.type == render_asset_type::texture) {
      auto texture = texture_for_publication(std::move(prepared.texture));
      if (texture.payload_source) {
        if (texture.profile.generation == 0U ||
            texture.payload.size() != texture.payload_source->size()) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
        candidate.texture_payload =
            std::make_shared<const std::vector<std::byte>>(std::move(texture.payload));
        texture.upload_payload = candidate.texture_payload;
      }
      created = texture.manifest.empty()
                    ? resources_.create_texture(std::move(texture), &rid)
                    : resources_.create_packaged_texture(std::move(texture), &rid);
      if (created != GNEISS_SUCCESS) {
        return created;
      }
      try {
        owned = std::make_shared<texture_asset>(resources_, rid);
      } catch (...) {
        (void)resources_.destroy_texture(rid);
        throw;
      }
      candidate.texture = resources_.share_texture(rid);
    } else if (prepared.source.type == render_asset_type::mesh) {
      created = prepared.shared_mesh
                    ? resources_.create_prepared_mesh(std::move(prepared.shared_mesh), &rid)
                    : resources_.create_prepared_mesh(std::move(prepared.mesh), &rid);
      if (created != GNEISS_SUCCESS) {
        return created;
      }
      try {
        owned = std::make_shared<mesh_asset>(resources_, rid);
      } catch (...) {
        (void)resources_.destroy_mesh(rid);
        throw;
      }
      candidate.mesh = resources_.share_mesh(rid);
    } else if (prepared.source.type == render_asset_type::material) {
      auto material = material_for_publication(prepared.material);
      for (std::size_t slot = 0; slot < prepared.texture_uris.size(); ++slot) {
        const auto& uri = prepared.texture_uris[slot];
        if (uri.empty()) {
          continue;
        }
        const auto found = std::ranges::find_if(staged, [&](const auto& other) {
          return other.source.uri == uri && other.source.type == render_asset_type::texture;
        });
        if (found == staged.end()) {
          return GNEISS_ERROR_INVALID_STATE;
        }
        if (fixed && (found->mode != publication_mode::fixed_version ||
                      found->version != prepared.texture_versions[slot])) {
          return GNEISS_ERROR_INVALID_STATE;
        }
        material.set_texture(slot, found->lease.get());
        candidate.dependencies[slot] = found->lease.entry_;
        candidate.dependency_textures[slot] = found->texture;
      }
      created = resources_.create_material(material, &rid);
      if (created != GNEISS_SUCCESS) {
        return created;
      }
      try {
        auto asset = std::make_shared<material_asset>(resources_, rid, candidate.dependencies);
        candidate.document = std::move(prepared.material_document);
        asset->document = candidate.document;
        owned = std::move(asset);
      } catch (...) {
        (void)resources_.destroy_material(rid);
        throw;
      }
      candidate.material = resources_.share_material(rid);
    } else {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (current) {
      candidate.lease.entry_ = current;
      const auto existing = candidate.lease.get();
      switch (prepared.source.type) {
      case render_asset_type::invalid:
        return GNEISS_ERROR_INVALID_ARGUMENT;
      case render_asset_type::texture:
        candidate.previous = resources_.share_texture(existing);
        break;
      case render_asset_type::mesh:
        candidate.previous = resources_.share_mesh(existing);
        break;
      case render_asset_type::material:
        candidate.previous = resources_.share_material(existing);
        break;
      }
      if (!owns(candidate.lease)) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
    } else {
      auto entry = std::make_shared<engine::resource::resource_cache::entry>();
      entry->uri = prepared.source.uri;
      entry->type = static_cast<std::uint32_t>(prepared.source.type);
      entry->state = engine::resource::resource_state::ready;
      entry->resource = std::move(owned);
      candidate.lease.entry_ = std::move(entry);
    }
    output = std::move(candidate);
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::publish_assets(std::span<asset_candidate> candidates) noexcept {
  try {
    const bool fixed =
        !candidates.empty() && candidates.front().mode == publication_mode::fixed_version;
    if (std::ranges::any_of(candidates, [&](const auto& candidate) {
          return (candidate.mode == publication_mode::fixed_version) != fixed;
        })) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    if (fixed) {
      std::vector<engine::resource::resource_cache::version_publication> versions;
      versions.reserve(candidates.size());
      for (const auto& candidate : candidates) {
        const auto rid = candidate.lease.get();
        if (!candidate.lease ||
            (candidate.texture && resources_.share_texture(rid) != candidate.texture) ||
            (candidate.mesh && resources_.share_mesh(rid) != candidate.mesh) ||
            (candidate.material && resources_.share_material(rid) != candidate.material)) {
          return GNEISS_ERROR_INVALID_STATE;
        }
        versions.push_back({.key = candidate.version, .value = candidate.lease.entry_});
      }
      return cache_.publish_versions(versions);
    }
    for (const auto& candidate : candidates) {
      const auto current = cache_.observe(candidate.source.uri).lock();
      std::shared_ptr<const void> previous;
      if (candidate.texture) {
        previous = resources_.share_texture(candidate.lease.get());
      } else if (candidate.mesh) {
        previous = resources_.share_mesh(candidate.lease.get());
      } else if (candidate.material) {
        previous = resources_.share_material(candidate.lease.get());
      }
      if (!owns(candidate.lease) || !current || current != candidate.expected.lock() ||
          previous != candidate.previous ||
          *update_sequence(candidate.lease) != candidate.update_sequence) {
        return GNEISS_ERROR_INVALID_STATE;
      }
    }
    // 所有分配和校验已经完成，以下共享指针交换不会失败。
    for (auto& candidate : candidates) {
      const auto rid = candidate.lease.get();
      if (candidate.texture) {
        (void)resources_.replace_texture(rid, candidate.texture);
      }
      if (candidate.mesh) {
        (void)resources_.replace_mesh(rid, candidate.mesh);
      }
      if (candidate.material) {
        std::static_pointer_cast<material_asset>(candidate.lease.entry_->resource)->textures =
            candidate.dependencies;
        std::static_pointer_cast<material_asset>(candidate.lease.entry_->resource)->document =
            candidate.document;
        (void)resources_.replace_material(rid, candidate.material);
      }
    }
    ++revision_;
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::acquire_mesh(std::string_view uri, mesh_asset_lease& out_lease,
                                                asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  engine::resource::asset_identity identity;
  render_asset_lease selected;
  const auto identified = file_system_.identify(uri, mesh_type, identity);
  const auto ready = identified == GNEISS_SUCCESS
                         ? acquire_selected(identity, default_profile_, selected)
                         : identified;
  if (ready == GNEISS_SUCCESS) {
    out_lease.entry_ = std::move(selected.entry_);
    return GNEISS_SUCCESS;
  }
  if (identified == GNEISS_SUCCESS && ready != GNEISS_ERROR_NOT_READY) {
    return ready;
  }
  const auto result = cache_.acquire(
      uri, mesh_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != GNEISS_SUCCESS) {
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
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        const engine::resource::mesh_view desc{.vertices = vertices,
                                               .normals = normals,
                                               .indices = indices,
                                               .tangents = tangents,
                                               .uv1 = uv1,
                                               .colors = colors};
        gneiss::engine::core::rid rid = gneiss::engine::core::null_rid;
        result = resources_.create_mesh(desc, &rid);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "创建 Mesh RID 失败");
          return result;
        }
        try {
          output = std::make_shared<mesh_asset>(resources_, rid);
        } catch (...) {
          (void)resources_.destroy_mesh(rid);
          throw;
        }
        return GNEISS_SUCCESS;
      },
      out_lease.entry_);
  if (result == GNEISS_SUCCESS) {
    const auto status = resources_.mesh_status(out_lease.get());
    if (status != GNEISS_SUCCESS) {
      out_lease = {};
      fail(out_diagnostic, status, "", "资产内容尚未就绪或句柄已失效");
      return status;
    }
  }
  if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
    fail(out_diagnostic, result, "", "获取 Mesh 资产失败");
  }
  return result;
}

gneiss_result render_asset_loader::acquire_material(std::string_view uri,
                                                    material_asset_lease& out_lease,
                                                    asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  engine::resource::asset_identity identity;
  render_asset_lease selected;
  const auto identified = file_system_.identify(uri, material_type, identity);
  const auto ready = identified == GNEISS_SUCCESS
                         ? acquire_selected(identity, default_profile_, selected)
                         : identified;
  if (ready == GNEISS_SUCCESS) {
    out_lease.entry_ = std::move(selected.entry_);
    return GNEISS_SUCCESS;
  }
  if (identified == GNEISS_SUCCESS && ready != GNEISS_ERROR_NOT_READY) {
    return ready;
  }
  const auto result = cache_.acquire(
      uri, material_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "无法通过 VFS 读取 Material");
          return result;
        }
        gneiss::engine::resource::material_source source;
        result = parse_material(bytes, source, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        auto material = material_for_publication(source.parameters);

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
          if (result != GNEISS_SUCCESS) {
            fail(out_diagnostic, result, std::string{"/"} + names[slot],
                 texture_diagnostic.message);
            return result;
          }
          material.set_texture(slot, texture.get());
          dependencies[slot] = texture.entry_;
        }
        gneiss::engine::core::rid rid = gneiss::engine::core::null_rid;
        result = resources_.create_material(material, &rid);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "创建 Material RID 失败");
          return result;
        }
        try {
          output = std::make_shared<material_asset>(resources_, rid, std::move(dependencies));
        } catch (...) {
          (void)resources_.destroy_material(rid);
          throw;
        }
        return GNEISS_SUCCESS;
      },
      out_lease.entry_);
  if (result == GNEISS_SUCCESS) {
    const auto status = resources_.material_status(out_lease.get());
    if (status != GNEISS_SUCCESS) {
      out_lease = {};
      fail(out_diagnostic, status, "", "资产内容尚未就绪或句柄已失效");
      return status;
    }
  }
  if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
    fail(out_diagnostic, result, "", "获取 Material 资产失败");
  }
  return result;
}

gneiss_result render_asset_loader::acquire_texture(std::string_view uri,
                                                   texture_asset_lease& out_lease,
                                                   asset_diagnostic& out_diagnostic) noexcept {
  out_lease = {};
  out_diagnostic = {};
  engine::resource::asset_identity identity;
  render_asset_lease selected;
  const auto identified = file_system_.identify(uri, texture_type, identity);
  const auto ready = identified == GNEISS_SUCCESS
                         ? acquire_selected(identity, default_profile_, selected)
                         : identified;
  if (ready == GNEISS_SUCCESS) {
    out_lease.entry_ = std::move(selected.entry_);
    return GNEISS_SUCCESS;
  }
  if (identified == GNEISS_SUCCESS && ready != GNEISS_ERROR_NOT_READY) {
    return ready;
  }
  const auto result = cache_.acquire(
      uri, texture_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        engine::resource::prepared_texture_data prepared;
        auto result = prepare_texture(file_system_, uri, prepared, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        gneiss::engine::core::rid rid = gneiss::engine::core::null_rid;
        auto texture = texture_for_publication(std::move(prepared));
        result = texture.manifest.empty()
                     ? resources_.create_texture(std::move(texture), &rid)
                     : resources_.create_packaged_texture(std::move(texture), &rid);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "创建 Texture RID 失败");
          return result;
        }
        try {
          output = std::make_shared<texture_asset>(resources_, rid);
        } catch (...) {
          (void)resources_.destroy_texture(rid);
          throw;
        }
        return GNEISS_SUCCESS;
      },
      out_lease.entry_);
  if (result == GNEISS_SUCCESS) {
    const auto status = resources_.texture_status(out_lease.get());
    if (status != GNEISS_SUCCESS) {
      out_lease = {};
      fail(out_diagnostic, status, "", "资产内容尚未就绪或句柄已失效");
      return status;
    }
  }
  if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
    fail(out_diagnostic, result, "", "获取 Texture 资产失败");
  }
  return result;
}

gneiss_result render_asset_loader::reload_assets(std::span<const render_asset_reload> assets,
                                                 asset_diagnostic& out_diagnostic) noexcept {
  out_diagnostic = {};
  if (assets.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    prepared_render_batch batch;
    auto result = prepare_render_assets(file_system_, assets, batch, out_diagnostic, {}, 256U,
                                        std::size_t{256U} * 1024U * 1024U, default_profile_);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::vector<asset_candidate> candidates;
    candidates.reserve(batch.assets.size());
    for (auto& prepared : batch.assets) {
      asset_candidate candidate;
      result = stage_asset(std::move(prepared), candidates, candidate);
      if (result != GNEISS_SUCCESS) {
        fail(out_diagnostic, result, "", "同步重载资产暂存失败");
        candidates.clear();
        release_unused();
        return result;
      }
      candidates.push_back(std::move(candidate));
    }
    result = publish_assets(candidates);
    if (result != GNEISS_SUCCESS) {
      fail(out_diagnostic, result, "", "同步重载资产发布失败");
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::engine::render

namespace gneiss::engine::render {
gneiss_result render_asset_loader::prepare_adoption(
    std::span<const render_asset_lease> assets,
    std::span<const engine::resource::asset_version_key> versions, texture_prepare_profile profile,
    std::unique_ptr<engine::resource::resource_cache::prepared_adoption>& output) noexcept try {
  if (versions.empty() || versions.size() != assets.size()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  std::vector<engine::resource::resource_cache::version_selection> selections;
  selections.reserve(versions.size());
  for (std::size_t i = 0; i < versions.size(); ++i) {
    if (!owns(assets[i])) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    selections.push_back(
        {.selection = selection_key(versions[i].identity, profile), .version = versions[i]});
  }
  return cache_.prepare_adoption(selections, output);
} catch (const std::bad_alloc&) {
  return GNEISS_ERROR_OUT_OF_MEMORY;
} catch (...) {
  return GNEISS_ERROR_INTERNAL;
}
} // namespace gneiss::engine::render
