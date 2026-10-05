// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render/render_asset_loader.h"

#include "engine/asset/mesh_binary.hpp"
#include "engine/asset/texture_container.hpp"
#include "engine/asset/virtual_file_system.hpp"
#include "render/render_asset_parsing.hpp"
#include "render/render_resource_service.h"

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

using namespace gneiss::render_internal::asset_parsing;

constexpr std::uint32_t mesh_type = 1;
constexpr std::uint32_t material_type = 2;
constexpr std::uint32_t texture_type = 3;

struct mesh_asset final {
  mesh_asset(gneiss::render_internal::render_resource_service& owner, gneiss_mesh value) noexcept
      : resources(&owner), rid(value) {}
  mesh_asset(const mesh_asset&) = delete;
  mesh_asset& operator=(const mesh_asset&) = delete;
  gneiss::render_internal::render_resource_service* resources;
  gneiss_mesh rid;
  ~mesh_asset() {
    if (resources != nullptr && rid != GNEISS_NULL_MESH) {
      (void)resources->destroy_mesh(rid);
    }
  }
};

struct material_asset final {
  material_asset(gneiss::render_internal::render_resource_service& owner, gneiss_material value,
                 std::array<std::shared_ptr<const gneiss::asset_internal::resource_cache::entry>, 5>
                     texture_dependencies = {}) noexcept
      : resources(&owner), rid(value), textures(std::move(texture_dependencies)) {}
  material_asset(const material_asset&) = delete;
  material_asset& operator=(const material_asset&) = delete;
  gneiss::render_internal::render_resource_service* resources;
  gneiss_material rid;
  std::array<std::shared_ptr<const gneiss::asset_internal::resource_cache::entry>, 5> textures;
  ~material_asset() {
    if (resources != nullptr && rid != GNEISS_NULL_MATERIAL) {
      (void)resources->destroy_material(rid);
    }
  }
};

struct texture_asset final {
  texture_asset(gneiss::render_internal::render_resource_service& owner,
                gneiss_texture value) noexcept
      : resources(&owner), rid(value) {}
  texture_asset(const texture_asset&) = delete;
  texture_asset& operator=(const texture_asset&) = delete;
  gneiss::render_internal::render_resource_service* resources;
  gneiss_texture rid;
  ~texture_asset() {
    if (resources != nullptr && rid != GNEISS_NULL_TEXTURE) {
      (void)resources->destroy_texture(rid);
    }
  }
};

} // namespace

namespace gneiss::render_internal {

gneiss_mesh mesh_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return GNEISS_NULL_MESH;
  }
  return std::static_pointer_cast<mesh_asset>(entry_->resource)->rid;
}

gneiss_material material_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return GNEISS_NULL_MATERIAL;
  }
  return std::static_pointer_cast<material_asset>(entry_->resource)->rid;
}

gneiss_texture texture_asset_lease::get() const noexcept {
  if (entry_ == nullptr || entry_->resource == nullptr) {
    return GNEISS_NULL_TEXTURE;
  }
  return std::static_pointer_cast<texture_asset>(entry_->resource)->rid;
}

render_asset_loader::render_asset_loader(const asset_internal::virtual_file_system& file_system,
                                         asset_internal::resource_cache& cache,
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
render_asset_type render_asset_lease::type() const noexcept {
  return entry_ ? static_cast<render_asset_type>(entry_->type) : render_asset_type::invalid;
}
texture_asset_lease
render_asset_loader::texture_lease(const render_asset_lease& lease) const noexcept {
  texture_asset_lease result;
  if (lease.type() == render_asset_type::texture) {
    result.entry_ = lease.entry_;
  }
  return result;
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
        current->state != asset_internal::resource_state::ready) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
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
    const auto rid = output.get();
    if ((source.type == render_asset_type::texture && resources_.get_texture(rid)) ||
        (source.type == render_asset_type::mesh && resources_.get_mesh(rid)) ||
        (source.type == render_asset_type::material && resources_.get_material(rid))) {
      return GNEISS_SUCCESS;
    }
    output = {};
    return GNEISS_ERROR_INVALID_HANDLE;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
}

gneiss_result render_asset_loader::stage_asset(prepared_render_asset prepared,
                                               std::span<const asset_candidate> staged,
                                               asset_candidate& output) noexcept {
  output = {};
  try {
    asset_candidate candidate;
    candidate.source = prepared.source;
    candidate.bytes = prepared.bytes;
    const auto current = cache_.observe(prepared.source.uri).lock();
    if (current && (current->type != static_cast<std::uint32_t>(prepared.source.type) ||
                    current->state != asset_internal::resource_state::ready)) {
      return GNEISS_ERROR_INVALID_ARGUMENT;
    }
    candidate.existed = static_cast<bool>(current);
    candidate.expected = current;
    std::shared_ptr<void> owned;
    std::uint64_t rid{};
    auto created = GNEISS_ERROR_INVALID_ARGUMENT;
    if (prepared.source.type == render_asset_type::texture) {
      if (prepared.texture.payload_source) {
        if (prepared.texture.profile.generation == 0U ||
            prepared.texture.payload.size() != prepared.texture.payload_source->size()) {
          return GNEISS_ERROR_INVALID_ARGUMENT;
        }
        candidate.texture_payload =
            std::make_shared<const std::vector<std::byte>>(std::move(prepared.texture.payload));
        prepared.texture.upload_payload = candidate.texture_payload;
      }
      created = prepared.texture.manifest.empty()
                    ? resources_.create_texture(std::move(prepared.texture), &rid)
                    : resources_.create_packaged_texture(std::move(prepared.texture), &rid);
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
      created = resources_.create_prepared_mesh(std::move(prepared.mesh), &rid);
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
        prepared.material.set_texture(slot, found->lease.get());
        candidate.dependencies[slot] = found->lease.entry_;
        candidate.dependency_textures[slot] = found->texture;
      }
      const auto desc = prepared.material.description();
      created = resources_.create_material(desc, &rid);
      if (created != GNEISS_SUCCESS) {
        return created;
      }
      try {
        owned = std::make_shared<material_asset>(resources_, rid, candidate.dependencies);
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
      if (!candidate.previous) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
    } else {
      auto entry = std::make_shared<asset_internal::resource_cache::entry>();
      entry->uri = prepared.source.uri;
      entry->type = static_cast<std::uint32_t>(prepared.source.type);
      entry->state = asset_internal::resource_state::ready;
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
    std::vector<asset_internal::resource_cache::reload_request> requests;
    for (const auto& candidate : candidates) {
      const auto current = cache_.observe(candidate.source.uri).lock();
      std::shared_ptr<const void> previous;
      if (candidate.texture) {
        previous = resources_.share_texture(candidate.lease.get());
      }
      if (candidate.mesh) {
        previous = resources_.share_mesh(candidate.lease.get());
      }
      if (candidate.material) {
        previous = resources_.share_material(candidate.lease.get());
      }
      if (!candidate.lease || !previous ||
          (candidate.existed &&
           (!current || current != candidate.expected.lock() || previous != candidate.previous)) ||
          (!candidate.existed && current)) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      if (!candidate.existed) {
        requests.push_back(
            {.uri = candidate.source.uri,
             .type = static_cast<std::uint32_t>(candidate.source.type),
             .load = [resource = candidate.lease.entry_->resource](auto&, auto& output) {
               output = resource;
               return GNEISS_SUCCESS;
             }});
      }
    }
    std::vector<std::shared_ptr<const asset_internal::resource_cache::entry>> committed;
    const auto result =
        requests.empty() ? GNEISS_SUCCESS : cache_.reload_transaction(requests, committed);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    std::size_t inserted{};
    for (auto& candidate : candidates) {
      if (!candidate.existed) {
        candidate.lease.entry_ = committed[inserted++];
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
        auto dependencies = candidate.dependencies;
        for (auto& dependency : dependencies) {
          if (!dependency) {
            continue;
          }
          const auto found = std::ranges::find_if(candidates, [&](const auto& other) {
            return other.source.uri == dependency->uri && other.texture;
          });
          if (found != candidates.end()) {
            dependency = found->lease.entry_;
          }
        }
        std::static_pointer_cast<material_asset>(candidate.lease.entry_->resource)->textures =
            std::move(dependencies);
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
  const auto result = cache_.acquire(
      uri, mesh_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "无法通过 VFS 读取 Mesh");
          return result;
        }
        std::vector<gneiss_mesh_vertex> vertices;
        std::vector<gneiss_mesh_normal> normals;
        std::vector<std::uint32_t> indices;
        std::vector<gneiss_mesh_tangent> tangents;
        std::vector<gneiss_mesh_uv> uv1;
        std::vector<gneiss_mesh_color> colors;
        result = gneiss::asset_internal::is_mesh_binary(bytes)
                     ? parse_binary_mesh(bytes, vertices, normals, indices, tangents, uv1, colors,
                                         out_diagnostic)
                     : parse_mesh(bytes, vertices, normals, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        const gneiss_mesh_desc desc{
            .struct_size = sizeof(gneiss_mesh_desc),
            .vertex_count = static_cast<std::uint32_t>(vertices.size()),
            .vertices = vertices.data(),
            .reserved = 0,
            .reserved_2 = 0,
            .normal_count = static_cast<std::uint32_t>(normals.size()),
            .normals = normals.empty() ? nullptr : normals.data(),
            .index_count = static_cast<std::uint32_t>(indices.size()),
            .reserved_3 = 0,
            .indices = indices.empty() ? nullptr : indices.data(),
            .tangent_count = static_cast<std::uint32_t>(tangents.size()),
            .reserved_4 = 0U,
            .tangents = tangents.empty() ? nullptr : tangents.data(),
            .uv1_count = static_cast<std::uint32_t>(uv1.size()),
            .reserved_5 = 0U,
            .uv1 = uv1.empty() ? nullptr : uv1.data(),
            .color_count = static_cast<std::uint32_t>(colors.size()),
            .reserved_6 = 0U,
            .colors = colors.empty() ? nullptr : colors.data(),
        };
        gneiss_mesh rid = GNEISS_NULL_MESH;
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
  const auto result = cache_.acquire(
      uri, material_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        std::vector<std::byte> bytes;
        auto result = file_system_.read(uri, bytes);
        if (result != GNEISS_SUCCESS) {
          fail(out_diagnostic, result, "", "无法通过 VFS 读取 Material");
          return result;
        }
        material_source source;
        result = parse_material(bytes, source, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        material_resource material{
            .red = source.color[0],
            .green = source.color[1],
            .blue = source.color[2],
            .alpha = source.color[3],
            .base_color_texture = GNEISS_NULL_TEXTURE,
            .metallic = source.metallic,
            .roughness = source.roughness,
        };
        material.normal_scale = source.normal_scale;
        material.occlusion_strength = source.occlusion_strength;
        material.emissive = source.emissive;
        material.alpha_mode = source.state.alpha_mode;
        material.double_sided = source.state.double_sided;
        material.alpha_cutoff = source.state.alpha_cutoff;
        material.sampling = source.state.sampling;

        std::array<std::shared_ptr<const asset_internal::resource_cache::entry>, 5> dependencies;
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
        const auto desc = material.description();
        gneiss_material rid = GNEISS_NULL_MATERIAL;
        result = resources_.create_material(desc, &rid);
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
  const auto result = cache_.acquire(
      uri, texture_type,
      [this, uri, &out_diagnostic](std::shared_ptr<void>& output) -> gneiss_result {
        texture_resource prepared;
        auto result = prepare_texture(file_system_, uri, prepared, out_diagnostic);
        if (result != GNEISS_SUCCESS) {
          return result;
        }
        gneiss_texture rid = GNEISS_NULL_TEXTURE;
        result = prepared.manifest.empty()
                     ? resources_.create_texture(std::move(prepared), &rid)
                     : resources_.create_packaged_texture(std::move(prepared), &rid);
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
  if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
    fail(out_diagnostic, result, "", "获取 Texture 资产失败");
  }
  return result;
}

gneiss_result render_asset_loader::observe_texture(std::string_view uri,
                                                   texture_target& output) const noexcept {
  output = {};
  try {
    output.uri = uri;
    output.expected = cache_.observe(uri);
    const auto current = output.expected.lock();
    output.existed = static_cast<bool>(current);
    return current && (current->type != texture_type ||
                       current->state != asset_internal::resource_state::ready)
               ? GNEISS_ERROR_INVALID_ARGUMENT
               : GNEISS_SUCCESS;
  } catch (...) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  }
}

gneiss_result render_asset_loader::stage_texture(const texture_target& target,
                                                 texture_resource prepared,
                                                 texture_candidate& output) noexcept {
  output = {};
  try {
    const auto current = cache_.observe(target.uri).lock();
    if ((target.existed && (!current || current != target.expected.lock())) ||
        (!target.existed && current)) {
      return GNEISS_ERROR_INVALID_STATE;
    }
    texture_candidate candidate;
    candidate.target = target;
    gneiss_texture staged = GNEISS_NULL_TEXTURE;
    const auto created = prepared.manifest.empty()
                             ? resources_.create_texture(std::move(prepared), &staged)
                             : resources_.create_packaged_texture(std::move(prepared), &staged);
    if (created != GNEISS_SUCCESS) {
      return created;
    }
    // 私有暂存 RID 只用于验证和所有权；失败时不会进入缓存。
    std::shared_ptr<texture_asset> owned;
    try {
      owned = std::make_shared<texture_asset>(resources_, staged);
    } catch (...) {
      (void)resources_.destroy_texture(staged);
      throw;
    }
    candidate.data = resources_.share_texture(staged);
    if (current) {
      candidate.lease.entry_ = current;
      candidate.previous = resources_.share_texture(candidate.lease.get());
      if (!candidate.previous) {
        return GNEISS_ERROR_INVALID_HANDLE;
      }
    } else {
      auto entry = std::make_shared<asset_internal::resource_cache::entry>();
      entry->uri = target.uri;
      entry->type = texture_type;
      entry->state = asset_internal::resource_state::ready;
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

gneiss_result
render_asset_loader::publish_textures(std::span<texture_candidate> candidates) noexcept {
  try {
    std::vector<asset_internal::resource_cache::reload_request> requests;
    requests.reserve(candidates.size());
    for (const auto& candidate : candidates) {
      const auto current = cache_.observe(candidate.target.uri).lock();
      if (!candidate.data || !candidate.lease ||
          (candidate.target.existed &&
           (!current || current != candidate.target.expected.lock() ||
            resources_.share_texture(candidate.lease.get()) != candidate.previous)) ||
          (!candidate.target.existed && current) ||
          !resources_.get_texture(candidate.lease.get())) {
        return GNEISS_ERROR_INVALID_STATE;
      }
      if (candidate.target.existed) {
        continue;
      }
      requests.push_back(
          {.uri = candidate.target.uri,
           .type = texture_type,
           .load = [resource = candidate.lease.entry_->resource](auto&, auto& output) {
             output = resource;
             return GNEISS_SUCCESS;
           }});
    }
    std::vector<std::shared_ptr<const asset_internal::resource_cache::entry>> committed;
    const auto result =
        requests.empty() ? GNEISS_SUCCESS : cache_.reload_transaction(requests, committed);
    if (result != GNEISS_SUCCESS) {
      return result;
    }
    ++revision_;
    // 缓存事务已完成全部可能分配的工作；这里仅交换已验证槽位的 shared_ptr，不会失败。
    std::size_t inserted{};
    for (auto& candidate : candidates) {
      if (!candidate.target.existed) {
        candidate.lease.entry_ = committed[inserted++];
      }
      (void)resources_.replace_texture(candidate.lease.get(), candidate.data);
    }
    return GNEISS_SUCCESS;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

gneiss_result render_asset_loader::reload_assets(std::span<const render_asset_reload> assets,
                                                 asset_diagnostic& out_diagnostic) noexcept {
  out_diagnostic = {};
  if (assets.empty()) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  try {
    std::vector<render_asset_reload> ordered(assets.begin(), assets.end());
    const auto priority = [](render_asset_type type) {
      switch (type) {
      case render_asset_type::invalid:
        return 3U;
      case render_asset_type::texture:
        return 0U;
      case render_asset_type::material:
        return 1U;
      case render_asset_type::mesh:
        return 2U;
      }
      return 3U;
    };
    std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& left, const auto& right) {
      return priority(left.type) < priority(right.type);
    });
    std::vector<asset_internal::resource_cache::reload_request> requests;
    requests.reserve(ordered.size());
    for (const auto& asset : ordered) {
      if (priority(asset.type) == 3U) {
        return GNEISS_ERROR_INVALID_ARGUMENT;
      }
      requests.push_back({.uri = asset.uri,
                          .type = static_cast<std::uint32_t>(asset.type),
                          .load = [this, uri = asset.uri, type = asset.type, &out_diagnostic](
                                      asset_internal::resource_cache& staging,
                                      std::shared_ptr<void>& output) -> gneiss_result {
                            render_asset_loader loader(file_system_, staging, resources_);
                            gneiss_result result = GNEISS_ERROR_INVALID_ARGUMENT;
                            if (type == render_asset_type::texture) {
                              texture_asset_lease lease;
                              result = loader.acquire_texture(uri, lease, out_diagnostic);
                              if (result == GNEISS_SUCCESS) {
                                output = lease.entry_->resource;
                              }
                            } else if (type == render_asset_type::material) {
                              material_asset_lease lease;
                              result = loader.acquire_material(uri, lease, out_diagnostic);
                              if (result == GNEISS_SUCCESS) {
                                output = lease.entry_->resource;
                              }
                            } else if (type == render_asset_type::mesh) {
                              mesh_asset_lease lease;
                              result = loader.acquire_mesh(uri, lease, out_diagnostic);
                              if (result == GNEISS_SUCCESS) {
                                output = lease.entry_->resource;
                              }
                            }
                            return result;
                          }});
    }
    std::vector<std::shared_ptr<const asset_internal::resource_cache::entry>> committed;
    const auto result = cache_.reload_transaction(requests, committed);
    if (result != GNEISS_SUCCESS && out_diagnostic.result == GNEISS_SUCCESS) {
      fail(out_diagnostic, result, "", "渲染资产事务重载失败");
    }
    return result;
  } catch (const std::bad_alloc&) {
    return GNEISS_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return GNEISS_ERROR_INTERNAL;
  }
}

} // namespace gneiss::render_internal
