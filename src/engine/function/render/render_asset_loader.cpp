// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/render/render_asset_loader.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/asset_manager.hpp"
#include <atomic>

#include "engine/function/render/render_resource_service.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/mesh/mesh_binary.hpp"
#include "engine/resource/texture/texture_decoder.hpp"

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

using namespace gneiss::engine;

// CPU 负载由资源表保留；在途上传另持有不可变数据引用。
[[nodiscard]] gneiss::engine::texture_data
make_texture_data(gneiss::engine::prepared_texture_data prepared) noexcept {
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
  };
}

gneiss::engine::type_id cache_type(gneiss::engine::render_asset_type type) noexcept {
  using gneiss::engine::render_asset_type;
  switch (type) {
  case render_asset_type::mesh:
    return gneiss::engine::runtime_type_of<mesh_resource>();
  case render_asset_type::material:
    return gneiss::engine::runtime_type_of<material_resource>();
  case render_asset_type::texture:
    return gneiss::engine::runtime_type_of<texture_resource>();
  default:
    return gneiss::engine::runtime_type_of<void>();
  }
}

} // namespace

namespace gneiss::engine {

void render_asset_loader::release_unused() noexcept {
  // 析构材质才会释放其纹理依赖，消费后重新扫描统一缓存。
  do {
    cache_.release_unused();
  } while (resources_.drain_retired_resources() != 0U);
}

render_asset_loader::render_asset_loader(asset_manager& assets,
                                         render_resource_service& resources) noexcept
    : assets_(assets), cache_(assets.cache()), resources_(resources) {}

render_asset_type asset_kind(const render_asset_ref& asset) noexcept {
  if (asset) {
    if (asset->type == gneiss::engine::runtime_type_of<mesh_resource>())
      return render_asset_type::mesh;
    if (asset->type == gneiss::engine::runtime_type_of<material_resource>())
      return render_asset_type::material;
    if (asset->type == gneiss::engine::runtime_type_of<texture_resource>())
      return render_asset_type::texture;
  }
  return render_asset_type::invalid;
}

gneiss::engine::rid asset_rid(const render_asset_ref& asset) noexcept {
  if (!asset || !asset->resource) {
    return 0U;
  }
  switch (asset_kind(asset)) {
  case render_asset_type::invalid:
    return 0U;
  case render_asset_type::mesh:
    return std::static_pointer_cast<const mesh_resource>(asset->resource)->rid_;
  case render_asset_type::material:
    return std::static_pointer_cast<const material_resource>(asset->resource)->rid_;
  case render_asset_type::texture:
    return std::static_pointer_cast<const texture_resource>(asset->resource)->rid_;
  }
  return 0U;
}
bool render_asset_loader::owns(const render_asset_ref& lease) const noexcept {
  gneiss::engine::asset_identity identity;
  if (!lease || !lease->resource ||
      assets_.identify(lease->uri, static_cast<std::uint32_t>(asset_kind(lease)), identity) !=
          gneiss::engine::result::success ||
      identity.source != lease->source) {
    return false;
  }
  switch (asset_kind(lease)) {
  case render_asset_type::mesh:
    return std::static_pointer_cast<const mesh_resource>(lease->resource)->resources_ ==
               &resources_ &&
           resources_.mesh_status(asset_rid(lease)) != gneiss::engine::result::invalid_handle;
  case render_asset_type::material:
    return std::static_pointer_cast<const material_resource>(lease->resource)->resources_ ==
               &resources_ &&
           resources_.material_status(asset_rid(lease)) != gneiss::engine::result::invalid_handle;
  case render_asset_type::texture:
    return std::static_pointer_cast<const texture_resource>(lease->resource)->resources_ ==
               &resources_ &&
           resources_.texture_status(asset_rid(lease)) != gneiss::engine::result::invalid_handle;
  default:
    return false;
  }
}

mesh render_asset_loader::mesh_ref(const render_asset_ref& value) noexcept {
  return asset_cast<mesh_resource>(value);
}
material render_asset_loader::material_ref(const render_asset_ref& value) noexcept {
  return asset_cast<material_resource>(value);
}

gneiss::engine::result
render_asset_loader::acquire_cached(const render_asset_reload& source, render_asset_ref& output,
                                    texture_prepare_profile profile) const noexcept {
  output = {};
  try {
    std::shared_ptr<const gneiss::engine::files::source_domain> domain;
    asset_identity identity;
    const auto identified =
        assets_.identify(source.uri, static_cast<std::uint32_t>(source.type), identity);
    domain = std::move(identity.source);
    if (identified != gneiss::engine::result::success)
      return identified;
    const auto current = cache_.observe(source.uri, cache_type(source.type), domain).lock();
    if (!current) {
      return gneiss::engine::result::not_found;
    }
    if (current->type != cache_type(source.type) ||
        current->state != gneiss::engine::resource_state::ready) {
      return gneiss::engine::result::invalid_argument;
    }
    render_asset_ref target;
    target = render_asset_ref{current};
    if (!owns(target)) {
      return gneiss::engine::result::invalid_handle;
    }
    auto status = gneiss::engine::result::invalid_argument;
    switch (source.type) {
    case render_asset_type::texture:
      status = resources_.texture_status(asset_rid(target));
      break;
    case render_asset_type::mesh:
      status = resources_.mesh_status(asset_rid(target));
      break;
    case render_asset_type::material:
      status = resources_.material_status(asset_rid(target));
      break;
    case render_asset_type::invalid:
      break;
    }
    if (status != gneiss::engine::result::success) {
      return status;
    }
    const auto matches_profile = [&](const texture_resource& resource) {
      const auto rid = resource.rid_;
      const auto* texture = resources_.get_texture(rid);
      return texture &&
             (profile.generation == 0U || texture->manifest.empty() || texture->profile == profile);
    };
    if (source.type == render_asset_type::texture &&
        !matches_profile(*current->as<texture_resource>())) {
      return gneiss::engine::result::not_found;
    }
    if (source.type == render_asset_type::material) {
      const auto material = std::static_pointer_cast<const material_resource>(current->resource);
      for (const auto& texture : material->textures_) {
        if (texture && !matches_profile(*texture)) {
          return gneiss::engine::result::not_found;
        }
      }
    }
    output = render_asset_ref{current};
    return gneiss::engine::result::success;
  } catch (...) {
    return gneiss::engine::result::out_of_memory;
  }
}

std::uint64_t render_asset_loader::next_update_sequence() noexcept {
  // 多个加载器可共享同一来源缓存；请求顺序不能在每个加载器中从头计数。
  static std::atomic_uint64_t sequence{};
  return sequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
}

std::uint64_t* render_asset_loader::update_sequence(const render_asset_ref& lease) const noexcept {
  if (!owns(lease)) {
    return nullptr;
  }
  switch (asset_kind(lease)) {
  case render_asset_type::mesh:
    return &std::static_pointer_cast<const mesh_resource>(lease->resource)->update_sequence_;
  case render_asset_type::texture:
    return &std::static_pointer_cast<const texture_resource>(lease->resource)->update_sequence_;
  case render_asset_type::material:
    return &std::static_pointer_cast<const material_resource>(lease->resource)->update_sequence_;
  case render_asset_type::invalid:
    return nullptr;
  }
  return nullptr;
}
bool render_asset_loader::claim_update(const render_asset_ref& lease,
                                       std::uint64_t sequence) noexcept {
  auto* current = update_sequence(lease);
  if (current == nullptr || sequence == 0U || sequence < *current) {
    return false;
  }
  *current = sequence;
  return true;
}

gneiss::engine::result render_asset_loader::reserve_asset(const render_asset_reload& source,
                                                          render_asset_ref& output) noexcept {
  output = {};
  try {
    if (gneiss::engine::validate_uri(source.uri) != gneiss::engine::result::success) {
      return gneiss::engine::result::invalid_argument;
    }
    gneiss::engine::asset_identity identity;
    const auto identified =
        assets_.identify(source.uri, static_cast<std::uint32_t>(source.type), identity);
    if (identified != gneiss::engine::result::success) {
      return identified;
    }
    const auto result = cache_.load(
        source.uri, cache_type(source.type),
        [&](std::shared_ptr<const void>& resource) {
          auto result = gneiss::engine::result::invalid_argument;
          switch (source.type) {
          case render_asset_type::mesh: {
            gneiss::engine::rid handle{};
            result = resources_.reserve_mesh(&handle);
            if (result.ok()) {
              try {
                auto value = resources_.reference_mesh(handle);
                value->identity_ = identity;
                resource = value.shared();
              } catch (...) {
                (void)resources_.destroy_mesh(handle);
                throw;
              }
            }
            break;
          }
          case render_asset_type::texture: {
            gneiss::engine::rid handle{};
            result = resources_.reserve_texture(&handle);
            if (result.ok()) {
              try {
                auto value = resources_.reference_texture(handle);
                value->identity_ = identity;
                resource = value.shared();
              } catch (...) {
                (void)resources_.destroy_texture(handle);
                throw;
              }
            }
            break;
          }
          case render_asset_type::material: {
            gneiss::engine::rid handle{};
            result = resources_.reserve_material(&handle);
            if (result.ok()) {
              try {
                auto value = resources_.reference_material(handle);
                value->identity_ = identity;
                resource = value.shared();
              } catch (...) {
                (void)resources_.destroy_material(handle);
                throw;
              }
            }
            break;
          }
          case render_asset_type::invalid:
            break;
          }
          return result;
        },
        output, identity.source);
    if (result == gneiss::engine::result::success && !owns(output)) {
      output.reset();
      return gneiss::engine::result::invalid_handle;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}

gneiss::engine::result render_asset_loader::prepare_material_update(prepared_render_asset& prepared,
                                                                    asset_update& upload) {
  gneiss::engine::material_data material{.parameters = prepared.material};
  for (std::size_t slot = 0; slot < prepared.texture_uris.size(); ++slot) {
    const auto& uri = prepared.texture_uris[slot];
    if (uri.empty()) {
      continue;
    }
    render_asset_ref dependency;
    const auto reserved =
        reserve_asset({.uri = uri, .type = render_asset_type::texture}, dependency);
    if (reserved != gneiss::engine::result::success) {
      return reserved;
    }
    material.set_texture(slot, asset_rid(dependency));
    upload.dependencies[slot] = dependency->as<texture_resource>();
  }
  return resources_.prepare_material_data(material, upload.data.material);
}

gneiss::engine::result render_asset_loader::prepare_update(prepared_render_asset prepared,
                                                           asset_update& output,
                                                           const render_asset_ref& target,
                                                           std::uint64_t sequence) noexcept try {
  output = {};
  asset_update upload;
  upload.target = target;
  if (!upload.target) {
    const auto reserved = reserve_asset(prepared.source, upload.target);
    if (reserved != gneiss::engine::result::success) {
      return reserved;
    }
  }
  if (gneiss::engine::asset_kind(upload.target) != prepared.source.type ||
      gneiss::engine::asset_uri(upload.target) != prepared.source.uri || !owns(upload.target)) {
    return gneiss::engine::result::invalid_argument;
  }
  for (const auto& input : prepared.sources) {
    gneiss::engine::asset_identity identity;
    if (assets_.identify(input.identity.uri, input.identity.type, identity) !=
            gneiss::engine::result::success ||
        identity != input.identity) {
      return gneiss::engine::result::invalid_argument;
    }
  }
  upload.sequence = sequence == 0U ? next_update_sequence() : sequence;
  if (!claim_update(upload.target, upload.sequence)) {
    return gneiss::engine::result::invalid_state;
  }
  upload.data.bytes = prepared.bytes;
  gneiss::engine::result result{gneiss::engine::result::invalid_argument};
  switch (prepared.source.type) {
  case render_asset_type::mesh:
    result = resources_.prepare_mesh_data(
        std::make_shared<const mesh_data>(std::move(prepared.mesh)), upload.data.mesh);
    break;
  case render_asset_type::texture: {
    auto texture = make_texture_data(std::move(prepared.texture));
    result = resources_.prepare_texture_data(std::move(texture), upload.data.texture);
    break;
  }
  case render_asset_type::material:
    result = prepare_material_update(prepared, upload);
    break;
  case render_asset_type::invalid:
    return gneiss::engine::result::invalid_argument;
  }
  if (result == gneiss::engine::result::success) {
    output = std::move(upload);
  }
  return result;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}

gneiss::engine::result render_asset_loader::apply_update(asset_update& upload) noexcept {
  if (!owns(upload.target) || *update_sequence(upload.target) != upload.sequence) {
    return gneiss::engine::result::invalid_state;
  }
  const auto rid = asset_rid(upload.target);
  if (upload.data.mesh) {
    return resources_.replace_mesh(rid, upload.data.mesh) ? gneiss::engine::result::success
                                                          : gneiss::engine::result::out_of_memory;
  }
  if (upload.data.texture) {
    return resources_.replace_texture(rid, upload.data.texture)
               ? gneiss::engine::result::success
               : gneiss::engine::result::out_of_memory;
  }
  if (!upload.data.material) {
    return gneiss::engine::result::invalid_argument;
  }
  if (!resources_.replace_material(rid, upload.data.material)) {
    return gneiss::engine::result::out_of_memory;
  }
  return gneiss::engine::result::success;
}

result render_asset_loader::acquire_mesh(std::string_view uri, gneiss::engine::mesh& output,
                                         asset_diagnostic& diagnostic) noexcept {
  render_asset_ref entry;
  const auto status = acquire_asset(render_asset_type::mesh, uri, entry, diagnostic);
  output = asset_cast<mesh_resource>(entry);
  return status;
}
bool render_asset_loader::owns(const gneiss::engine::mesh& value) const noexcept {
  if (!value || value->resources_ != &resources_ ||
      resources_.mesh_status(resource_handle(value)) == result::invalid_handle)
    return false;
  asset_identity identity;
  return assets_
             .identify(value->source().uri, static_cast<std::uint32_t>(render_asset_type::mesh),
                       identity)
             .ok() &&
         identity == value->source();
}
result render_asset_loader::acquire_texture(std::string_view uri, gneiss::engine::texture& output,
                                            asset_diagnostic& diagnostic) noexcept {
  render_asset_ref entry;
  const auto status = acquire_asset(render_asset_type::texture, uri, entry, diagnostic);
  output = asset_cast<texture_resource>(entry);
  return status;
}
bool render_asset_loader::owns(const gneiss::engine::texture& value) const noexcept {
  if (!value || value->resources_ != &resources_ ||
      resources_.texture_status(resource_handle(value)) == result::invalid_handle)
    return false;
  asset_identity identity;
  return assets_
             .identify(value->source().uri, static_cast<std::uint32_t>(render_asset_type::texture),
                       identity)
             .ok() &&
         identity == value->source();
}
result render_asset_loader::acquire_material(std::string_view uri, gneiss::engine::material& output,
                                             asset_diagnostic& diagnostic) noexcept {
  render_asset_ref entry;
  const auto status = acquire_asset(render_asset_type::material, uri, entry, diagnostic);
  output = asset_cast<material_resource>(entry);
  return status;
}
bool render_asset_loader::owns(const gneiss::engine::material& value) const noexcept {
  if (!value || value->resources_ != &resources_ ||
      resources_.material_status(resource_handle(value)) == result::invalid_handle)
    return false;
  asset_identity identity;
  return assets_
             .identify(value->source().uri, static_cast<std::uint32_t>(render_asset_type::material),
                       identity)
             .ok() &&
         identity == value->source();
}
result render_asset_loader::acquire_asset(render_asset_type type, std::string_view uri,
                                          render_asset_ref& output,
                                          asset_diagnostic& diagnostic) noexcept try {
  output = {};
  diagnostic = {};
  const render_asset_reload source{std::string(uri), type};
  return acquire_cached(source, output, default_profile_);
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

} // namespace gneiss::engine
