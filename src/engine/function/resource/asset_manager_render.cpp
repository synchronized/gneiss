// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/asset_render_state.hpp"
#include "engine/function/resource/detail/render_resource_factory.hpp"
#include "engine/resource/material/detail/material_resource_access.hpp"
#include "engine/resource/mesh/detail/mesh_resource_access.hpp"
#include "engine/resource/texture/detail/texture_resource_access.hpp"
#include <atomic>

#include "engine/function/render/render_service.hpp"
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
#include <type_traits>
#include <vector>

namespace {

using namespace gneiss::engine;

gneiss::engine::type_id cache_type(gneiss::engine::asset_type type) noexcept {
  using gneiss::engine::asset_type;
  switch (type) {
  case asset_type::mesh:
    return gneiss::engine::runtime_type_of<mesh_resource>();
  case asset_type::material:
    return gneiss::engine::runtime_type_of<material_resource>();
  case asset_type::texture:
    return gneiss::engine::runtime_type_of<texture_resource>();
  default:
    return gneiss::engine::runtime_type_of<void>();
  }
}

} // namespace

namespace gneiss::engine {

asset_type asset_kind(const render_asset_ref& asset) noexcept {
  if (asset) {
    if (asset->type == engine::runtime_type_of<mesh_resource>())
      return asset_type::mesh;
    if (asset->type == engine::runtime_type_of<material_resource>())
      return asset_type::material;
    if (asset->type == engine::runtime_type_of<texture_resource>())
      return asset_type::texture;
  }
  return asset_type::invalid;
}

engine::rid asset_rid(const render_asset_ref& asset) noexcept {
  if (!asset || !asset->resource) {
    return 0U;
  }
  switch (asset_kind(asset)) {
  case asset_type::invalid:
    return 0U;
  case asset_type::mesh:
    return detail::mesh_resource_access::handle(
        *std::static_pointer_cast<const mesh_resource>(asset->resource));
  case asset_type::material:
    return detail::material_resource_access::handle(
        *std::static_pointer_cast<const material_resource>(asset->resource));
  case asset_type::texture:
    return detail::texture_resource_access::handle(
        *std::static_pointer_cast<const texture_resource>(asset->resource));
  }
  return 0U;
}

result asset_manager::attach_render(render_service& render, texture_prepare_profile profile) {
  if (render_stopping_)
    return result::invalid_state;
  if (render_state_)
    return &render_state_->factory.render() == &render ? result::success : result::invalid_state;
  render_state_ = std::make_unique<render_state>(render);
  render_state_->default_profile = profile;
  return result::success;
}
result asset_manager::configure_loading(task_executor& executor, texture_prepare_profile profile,
                                        std::shared_ptr<progress_notification> notification) {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  attach_task_executor(executor);
  if (render_state_->batch)
    return result::success;
  render_state_->default_profile = profile;
  render_state_->batch =
      std::make_unique<asset_load_batch>(executor, *this, profile, std::move(notification));
  return result::success;
}

void asset_manager::advance_render() {
  if (!render_state_)
    return;
  (void)render_state_->factory.drain();
  if (render_state_->batch)
    render_state_->batch->advance();
}
void asset_manager::stop_render() noexcept {
  render_stopping_ = true;
  if (render_state_)
    render_state_->batch.reset();
}
void asset_manager::request_stop_render() {
  render_stopping_ = true;
  if (render_state_ && render_state_->batch)
    render_state_->batch->request_stop();
}
bool asset_manager::render_stopped() const {
  return !render_state_ || !render_state_->batch || render_state_->batch->stopped();
}

result asset_manager::reserve_mesh(mesh& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.reserve_mesh(out);
}
mesh asset_manager::find_mesh(rid id) const noexcept {
  return render_state_ ? render_state_->factory.find_mesh(id) : mesh{};
}

result asset_manager::reserve_texture(texture& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.reserve_texture(out);
}
texture asset_manager::find_texture(rid id) const noexcept {
  return render_state_ ? render_state_->factory.find_texture(id) : texture{};
}

result asset_manager::reserve_material(material& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.reserve_material(out);
}
material asset_manager::find_material(rid id) const noexcept {
  return render_state_ ? render_state_->factory.find_material(id) : material{};
}

result asset_manager::create_mesh(const mesh_view& data, mesh& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.create_mesh(data, out);
}
result asset_manager::create_texture(const texture_view& data, texture& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.create_texture(data, out);
}
result asset_manager::create_material(const material_parameters& data, std::array<texture, 5> deps,
                                      material& out) noexcept {
  if (!render_state_ || render_stopping_) {
    out.reset();
    return result::invalid_state;
  }
  return render_state_->factory.create_material(data, std::move(deps), out);
}

result asset_manager::submit_assets(std::span<const asset_source> sources, std::uint64_t session,
                                    std::uint64_t revision, std::uint64_t& request, bool reload,
                                    std::size_t limit) {
  if (!render_state_ || !render_state_->batch || render_stopping_) {
    request = 0U;
    return result::invalid_state;
  }
  return render_state_->batch->submit_assets(sources, session, revision, request, reload, limit);
}
result asset_manager::submit(std::span<const std::string> uris, std::uint64_t session,
                             std::uint64_t revision, std::uint64_t& request, bool reload) {
  if (!render_state_ || !render_state_->batch || render_stopping_) {
    request = 0U;
    return result::invalid_state;
  }
  return render_state_->batch->submit(uris, session, revision, request, reload);
}
bool asset_manager::progress(asset_load_progress& out) const {
  return render_state_ && render_state_->batch && render_state_->batch->progress(out);
}
bool asset_manager::take(asset_load_completion& out) {
  return render_state_ && render_state_->batch && render_state_->batch->take(out);
}
bool asset_manager::busy() const noexcept {
  return render_state_ && render_state_->batch && render_state_->batch->busy();
}
bool asset_manager::cancel() {
  return render_state_ && render_state_->batch && render_state_->batch->cancel();
}
texture_prepare_profile asset_manager::profile() const noexcept {
  return render_state_ && render_state_->batch ? render_state_->batch->profile()
                                               : texture_prepare_profile{};
}
std::span<const render_asset_ref> asset_manager::requested_assets() const {
  return render_state_ && render_state_->batch ? render_state_->batch->requested_assets()
                                               : std::span<const render_asset_ref>{};
}

void asset_manager::release_cached_resources() noexcept {
  if (!render_state_) {
    cache_.release_unused();
    return;
  }
  // 析构材质才会释放其纹理依赖，消费后重新扫描统一缓存。
  do {
    cache_.release_unused();
  } while (render_state_->factory.drain() != 0U);
}

bool asset_manager::owns(const render_asset_ref& lease) const noexcept {
  if (!render_state_)
    return false;
  asset_identity identity;
  if (!lease || !lease->resource ||
      identify(lease->uri, static_cast<std::uint32_t>(asset_kind(lease)), identity) !=
          engine::result::success ||
      identity.source != lease->source) {
    return false;
  }
  switch (asset_kind(lease)) {
  case asset_type::mesh:
    return render_state_->factory.render().mesh_status(asset_rid(lease)) !=
           engine::result::invalid_handle;
  case asset_type::material:
    return render_state_->factory.render().material_status(asset_rid(lease)) !=
           engine::result::invalid_handle;
  case asset_type::texture:
    return render_state_->factory.render().texture_status(asset_rid(lease)) !=
           engine::result::invalid_handle;
  default:
    return false;
  }
}

engine::result asset_manager::acquire_cached(const asset_source& source, render_asset_ref& output,
                                             texture_prepare_profile profile) const noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  output = {};
  try {
    std::shared_ptr<const files::source_domain> domain;
    asset_identity identity;
    const auto identified = identify(source.uri, static_cast<std::uint32_t>(source.type), identity);
    domain = std::move(identity.source);
    if (identified != engine::result::success)
      return identified;
    const auto current = cache_.observe(source.uri, cache_type(source.type), domain).lock();
    if (!current) {
      return engine::result::not_found;
    }
    if (current->type != cache_type(source.type) ||
        current->state != engine::resource_state::ready) {
      return engine::result::invalid_argument;
    }
    render_asset_ref target;
    target = render_asset_ref{current};
    if (!owns(target)) {
      return engine::result::invalid_handle;
    }
    auto status = engine::result::invalid_argument;
    switch (source.type) {
    case asset_type::texture:
      status = render_state_->factory.render().texture_status(asset_rid(target));
      break;
    case asset_type::mesh:
      status = render_state_->factory.render().mesh_status(asset_rid(target));
      break;
    case asset_type::material:
      status = render_state_->factory.render().material_status(asset_rid(target));
      break;
    case asset_type::invalid:
      break;
    }
    if (status != engine::result::success) {
      return status;
    }
    const auto matches_profile = [&](const texture_resource& resource) {
      const auto rid = detail::texture_resource_access::handle(resource);
      const auto* texture = render_state_->factory.render().get_texture(rid);
      return texture &&
             (profile.generation == 0U || texture->manifest.empty() || texture->profile == profile);
    };
    if (source.type == asset_type::texture && !matches_profile(*current->as<texture_resource>())) {
      return engine::result::not_found;
    }
    if (source.type == asset_type::material) {
      const auto material = std::static_pointer_cast<const material_resource>(current->resource);
      for (const auto& texture : material->textures()) {
        if (texture && !matches_profile(*texture)) {
          return engine::result::not_found;
        }
      }
    }
    output = render_asset_ref{current};
    return engine::result::success;
  } catch (...) {
    return engine::result::out_of_memory;
  }
}

std::uint64_t detail::asset_update::next_update_sequence() noexcept {
  // 进程内单调请求序号，仅用于拒绝迟到的单项结果。
  static std::atomic_uint64_t sequence{};
  return sequence.fetch_add(1U, std::memory_order_relaxed) + 1U;
}

std::uint64_t detail::asset_update::update_sequence(asset_manager& assets,
                                                    const render_asset_ref& lease) noexcept {
  if (!assets.render_state_ || assets.render_stopping_)
    return 0U;
  if (!assets.owns(lease)) {
    return 0U;
  }
  switch (asset_kind(lease)) {
  case asset_type::mesh:
    return detail::mesh_resource_access::update_sequence(
        *std::static_pointer_cast<const mesh_resource>(lease->resource));
  case asset_type::texture:
    return detail::texture_resource_access::update_sequence(
        *std::static_pointer_cast<const texture_resource>(lease->resource));
  case asset_type::material:
    return detail::material_resource_access::update_sequence(
        *std::static_pointer_cast<const material_resource>(lease->resource));
  case asset_type::invalid:
    return 0U;
  }
  return 0U;
}

bool detail::asset_update::claim_update(asset_manager& assets, const render_asset_ref& lease,
                                        std::uint64_t sequence) noexcept {
  if (!assets.render_state_ || assets.render_stopping_)
    return false;
  if (!assets.owns(lease))
    return false;
  switch (asset_kind(lease)) {
  case asset_type::mesh:
    return detail::mesh_resource_access::claim_update(
        *std::static_pointer_cast<const mesh_resource>(lease->resource), sequence);
  case asset_type::texture:
    return detail::texture_resource_access::claim_update(
        *std::static_pointer_cast<const texture_resource>(lease->resource), sequence);
  case asset_type::material:
    return detail::material_resource_access::claim_update(
        *std::static_pointer_cast<const material_resource>(lease->resource), sequence);
  case asset_type::invalid:
    return false;
  }
  return false;
}

engine::result asset_manager::reserve_asset(const asset_source& source,
                                            render_asset_ref& output) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  output = {};
  try {
    if (engine::validate_uri(source.uri) != engine::result::success) {
      return engine::result::invalid_argument;
    }
    asset_identity identity;
    const auto identified = identify(source.uri, static_cast<std::uint32_t>(source.type), identity);
    if (identified != engine::result::success) {
      return identified;
    }
    const auto result = cache_.load(
        source.uri, cache_type(source.type),
        [&](std::shared_ptr<const void>& resource) {
          auto result = engine::result::invalid_argument;
          switch (source.type) {
          case asset_type::mesh: {
            engine::mesh value;
            result = render_state_->factory.reserve_mesh(value);
            if (result.ok()) {
              detail::mesh_resource_access::set_source(*value, identity);
              resource = value.shared();
            }
            break;
          }
          case asset_type::texture: {
            engine::texture value;
            result = render_state_->factory.reserve_texture(value);
            if (result.ok()) {
              detail::texture_resource_access::set_source(*value, identity);
              resource = value.shared();
            }
            break;
          }
          case asset_type::material: {
            engine::material value;
            result = render_state_->factory.reserve_material(value);
            if (result.ok()) {
              detail::material_resource_access::set_source(*value, identity);
              resource = value.shared();
            }
            break;
          }
          case asset_type::invalid:
            break;
          }
          return result;
        },
        output, identity.source);
    if (result == engine::result::success && !owns(output)) {
      output.reset();
      return engine::result::invalid_handle;
    }
    return result;
  } catch (const std::bad_alloc&) {
    return engine::result::out_of_memory;
  } catch (...) {
    return engine::result::internal;
  }
}

engine::result detail::asset_update::prepare_material_update(
    asset_manager& assets, const material_description& description, asset_update& upload) {
  if (!assets.render_state_ || assets.render_stopping_)
    return result::invalid_state;
  asset_update::material_content content;
  material_data material{.parameters = description.parameters};
  for (std::size_t slot = 0; slot < description.texture_uris.size(); ++slot) {
    const auto& uri = description.texture_uris[slot];
    if (uri.empty()) {
      continue;
    }
    render_asset_ref dependency;
    const auto reserved =
        assets.reserve_asset({.uri = uri, .type = asset_type::texture}, dependency);
    if (reserved != engine::result::success) {
      return reserved;
    }
    material.set_texture(slot, asset_rid(dependency));
    content.dependencies[slot] = dependency->as<texture_resource>();
  }
  const auto status =
      assets.render_state_->factory.render().prepare_material_data(material, content.data);
  if (status.ok())
    upload.data = std::move(content);
  return status;
}

engine::result detail::asset_update::prepare_update(asset_manager& assets, decoded_asset prepared,
                                                    asset_update& output,
                                                    const render_asset_ref& target,
                                                    std::uint64_t sequence) noexcept try {
  if (!assets.render_state_ || assets.render_stopping_)
    return result::invalid_state;
  output = {};
  const auto kind = prepared.kind();
  if (kind == asset_type::invalid)
    return result::invalid_argument;
  if (const auto* material = std::get_if<asset_ref<const material_description>>(&prepared.content);
      material && !*material)
    return result::invalid_argument;
  asset_update upload;
  upload.target = target;
  if (!upload.target) {
    const auto reserved = assets.reserve_asset({prepared.uri, kind}, upload.target);
    if (reserved != engine::result::success) {
      return reserved;
    }
  }
  if (engine::asset_kind(upload.target) != kind ||
      engine::asset_uri(upload.target) != prepared.uri || !assets.owns(upload.target)) {
    return engine::result::invalid_argument;
  }
  for (const auto& input : prepared.sources) {
    asset_identity identity;
    if (assets.identify(input.identity.uri, input.identity.type, identity) !=
            engine::result::success ||
        identity != input.identity) {
      return engine::result::invalid_argument;
    }
  }
  upload.sequence = sequence == 0U ? next_update_sequence() : sequence;
  if (!claim_update(assets, upload.target, upload.sequence)) {
    return engine::result::invalid_state;
  }
  const auto result = std::visit(
      [&](auto& content) -> engine::result {
        using T = std::decay_t<decltype(content)>;
        if constexpr (std::is_same_v<T, mesh_data>) {
          std::shared_ptr<const mesh_data> data;
          const auto status = assets.render_state_->factory.render().prepare_mesh_data(
              std::make_shared<const mesh_data>(std::move(content)), data);
          if (status.ok())
            upload.data = std::move(data);
          return status;
        } else if constexpr (std::is_same_v<T, texture_data>) {
          std::shared_ptr<const texture_data> data;
          const auto status =
              assets.render_state_->factory.render().prepare_texture_data(std::move(content), data);
          if (status.ok())
            upload.data = std::move(data);
          return status;
        } else if constexpr (std::is_same_v<T, asset_ref<const material_description>>) {
          return prepare_material_update(assets, *content, upload);
        } else {
          return engine::result::invalid_argument;
        }
      },
      prepared.content);
  if (result == engine::result::success) {
    output = std::move(upload);
  }
  return result;
} catch (const std::bad_alloc&) {
  return engine::result::out_of_memory;
} catch (...) {
  return engine::result::internal;
}

engine::result detail::asset_update::apply_update(asset_manager& assets,
                                                  asset_update& upload) noexcept {
  if (!assets.render_state_ || assets.render_stopping_)
    return result::invalid_state;
  if (!assets.owns(upload.target) || update_sequence(assets, upload.target) != upload.sequence) {
    return engine::result::invalid_state;
  }
  return std::visit(
      [&](const auto& content) -> result {
        using T = std::decay_t<decltype(content)>;
        bool adopted{};
        if constexpr (std::is_same_v<T, std::shared_ptr<const mesh_data>>) {
          if (!content || asset_kind(upload.target) != asset_type::mesh)
            return result::invalid_argument;
          adopted = assets.render_state_->factory.update_mesh(
              asset_cast<mesh_resource>(upload.target), content);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const texture_data>>) {
          if (!content || asset_kind(upload.target) != asset_type::texture)
            return result::invalid_argument;
          adopted = assets.render_state_->factory.update_texture(
              asset_cast<texture_resource>(upload.target), content);
        } else if constexpr (std::is_same_v<T, material_content>) {
          if (!content.data || asset_kind(upload.target) != asset_type::material)
            return result::invalid_argument;
          adopted = assets.render_state_->factory.update_material(
              asset_cast<material_resource>(upload.target), content.data, content.dependencies);
        } else {
          return result::invalid_argument;
        }
        return adopted ? result::success : result::out_of_memory;
      },
      upload.data);
}

result asset_manager::acquire_mesh(std::string_view uri, engine::mesh& output,
                                   asset_diagnostic& diagnostic) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  render_asset_ref entry;
  const auto status = acquire_asset(asset_type::mesh, uri, entry, diagnostic);
  output = asset_cast<mesh_resource>(entry);
  return status;
}

bool asset_manager::owns(const engine::mesh& value) const noexcept {
  if (!render_state_)
    return false;
  if (!value ||
      render_state_->factory.render().mesh_status(resource_handle(value)) == result::invalid_handle)
    return false;
  asset_identity identity;
  return identify(value->source().uri, static_cast<std::uint32_t>(asset_type::mesh), identity)
             .ok() &&
         identity == value->source();
}

result asset_manager::acquire_texture(std::string_view uri, engine::texture& output,
                                      asset_diagnostic& diagnostic) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  render_asset_ref entry;
  const auto status = acquire_asset(asset_type::texture, uri, entry, diagnostic);
  output = asset_cast<texture_resource>(entry);
  return status;
}

bool asset_manager::owns(const engine::texture& value) const noexcept {
  if (!render_state_)
    return false;
  if (!value || render_state_->factory.render().texture_status(resource_handle(value)) ==
                    result::invalid_handle)
    return false;
  asset_identity identity;
  return identify(value->source().uri, static_cast<std::uint32_t>(asset_type::texture), identity)
             .ok() &&
         identity == value->source();
}

result asset_manager::acquire_material(std::string_view uri, engine::material& output,
                                       asset_diagnostic& diagnostic) noexcept {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  render_asset_ref entry;
  const auto status = acquire_asset(asset_type::material, uri, entry, diagnostic);
  output = asset_cast<material_resource>(entry);
  return status;
}

bool asset_manager::owns(const engine::material& value) const noexcept {
  if (!render_state_)
    return false;
  if (!value || render_state_->factory.render().material_status(resource_handle(value)) ==
                    result::invalid_handle)
    return false;
  asset_identity identity;
  return identify(value->source().uri, static_cast<std::uint32_t>(asset_type::material), identity)
             .ok() &&
         identity == value->source();
}

result asset_manager::acquire_asset(asset_type type, std::string_view uri, render_asset_ref& output,
                                    asset_diagnostic& diagnostic) noexcept try {
  if (!render_state_ || render_stopping_)
    return result::invalid_state;
  output = {};
  diagnostic = {};
  const asset_source source{std::string(uri), type};
  return acquire_cached(source, output, render_state_->default_profile);
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
} // namespace gneiss::engine
