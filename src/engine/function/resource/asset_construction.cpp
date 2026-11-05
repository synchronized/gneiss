// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/resource/detail/asset_construction.hpp"
#include "engine/core/error.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/resource/material/detail/material_resource_access.hpp"
#include "engine/resource/mesh/detail/mesh_resource_access.hpp"
#include "engine/resource/texture/detail/texture_resource_access.hpp"

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

gneiss::engine::type_id cache_type(gneiss::engine::render_resource_kind type) noexcept {
  using gneiss::engine::render_resource_kind;
  switch (type) {
  case render_resource_kind::mesh:
    return gneiss::engine::runtime_type_of<mesh_resource>();
  case render_resource_kind::material:
    return gneiss::engine::runtime_type_of<material_resource>();
  case render_resource_kind::texture:
    return gneiss::engine::runtime_type_of<texture_resource>();
  default:
    return gneiss::engine::runtime_type_of<void>();
  }
}

} // namespace

namespace gneiss::engine {
asset_manager::implementation::implementation(render_service& render)
    : render_(render), retirement_(&render) {}
asset_manager::implementation::~implementation() { close(); }

template <typename T> std::shared_ptr<T> asset_manager::implementation::make(rid handle) {
  auto finalize = [](T& value, void* owner) noexcept {
    auto* render = static_cast<render_service*>(owner);
    if (!render)
      return;
    if constexpr (std::is_same_v<T, mesh_resource>) {
      const auto handle = detail::mesh_resource_access::handle(value);
      if (render->mesh_status(handle) != error::invalid_handle)
        (void)render->destroy_mesh(handle);
    } else if constexpr (std::is_same_v<T, texture_resource>) {
      const auto handle = detail::texture_resource_access::handle(value);
      if (render->texture_status(handle) != error::invalid_handle)
        (void)render->destroy_texture(handle);
    } else if constexpr (std::is_same_v<T, material_resource>) {
      const auto handle = detail::material_resource_access::handle(value);
      if (render->material_status(handle) != error::invalid_handle)
        (void)render->destroy_material(handle);
    }
  };
  if constexpr (std::is_same_v<T, mesh_resource>)
    return detail::mesh_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, texture_resource>)
    return detail::texture_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, material_resource>)
    return detail::material_resource_access::make(retirement_, finalize, handle);
}

void asset_manager::implementation::close() noexcept {
  if (closed_)
    return;
  closed_ = true;
  retirement_.close();
  for (const auto& [handle, value] : meshes_)
    if (render_.mesh_status(handle) != error::invalid_handle)
      (void)render_.destroy_mesh(handle);
  for (const auto& [handle, value] : textures_)
    if (render_.texture_status(handle) != error::invalid_handle)
      (void)render_.destroy_texture(handle);
  for (const auto& [handle, value] : materials_)
    if (render_.material_status(handle) != error::invalid_handle)
      (void)render_.destroy_material(handle);
  meshes_.clear();
  textures_.clear();
  materials_.clear();
}
mesh asset_manager::implementation::wrap_mesh(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = meshes_.try_emplace(handle).first;
  auto value = make<mesh_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_mesh(handle))
    update_mesh(handle, *data);
  return mesh{std::move(value)};
}
texture asset_manager::implementation::wrap_texture(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = textures_.try_emplace(handle).first;
  auto value = make<texture_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_texture(handle))
    update_texture(handle, *data);
  return texture{std::move(value)};
}
material asset_manager::implementation::wrap_material(rid handle) {
  const auto entry = materials_.try_emplace(handle).first;
  auto value = make<material_resource>(handle);
  entry->second = value;
  return material{std::move(value)};
}
void asset_manager::implementation::update_mesh(rid handle, const mesh_data& data) noexcept {
  const auto found = meshes_.find(handle);
  if (found != meshes_.end())
    if (auto value = found->second.lock())
      detail::mesh_resource_access::set_metadata(
          *value, mesh_metadata{data.vertices.size(), data.indices.size(), !data.normals.empty(),
                                !data.uv1.empty()});
}
void asset_manager::implementation::update_texture(rid handle, const texture_data& data) noexcept {
  const auto found = textures_.find(handle);
  if (found != textures_.end())
    if (auto value = found->second.lock())
      detail::texture_resource_access::set_metadata(
          *value,
          texture_metadata{
              data.width, data.height,
              data.levels.empty() ? std::nullopt
                                  : std::optional{static_cast<std::uint32_t>(data.levels.size())},
              data.manifest.empty() ? std::optional{data.format} : std::nullopt, data.color_space});
}
void asset_manager::implementation::update_material(rid handle, const material_data& data,
                                                    std::array<texture, 5> dependencies) noexcept {
  const auto found = materials_.find(handle);
  if (found != materials_.end())
    if (auto value = found->second.lock()) {
      detail::material_resource_access::set_content(*value, data.parameters,
                                                    std::move(dependencies));
    }
}
template <typename Ref, typename Create>
result<Ref> asset_manager::implementation::create(Create create_rid) noexcept {
  if (closed_)
    return error::invalid_state;
  auto created = create_rid();
  if (!created)
    return created.error();
  const auto handle = *created;
  auto cleanup = [&] {
    if constexpr (std::is_same_v<Ref, mesh>) {
      if (render_.mesh_status(handle) != error::invalid_handle)
        (void)render_.destroy_mesh(handle);
      meshes_.erase(handle);
    } else if constexpr (std::is_same_v<Ref, texture>) {
      if (render_.texture_status(handle) != error::invalid_handle)
        (void)render_.destroy_texture(handle);
      textures_.erase(handle);
    } else {
      if (render_.material_status(handle) != error::invalid_handle)
        (void)render_.destroy_material(handle);
      materials_.erase(handle);
    }
  };
  try {
    if constexpr (std::is_same_v<Ref, mesh>)
      return wrap_mesh(handle);
    else if constexpr (std::is_same_v<Ref, texture>)
      return wrap_texture(handle);
    else
      return wrap_material(handle);
  } catch (const std::bad_alloc&) {
    cleanup();
    return error::out_of_memory;
  } catch (...) {
    cleanup();
    return error::internal;
  }
}
result<mesh> asset_manager::implementation::reserve_mesh() noexcept {
  return create<mesh>([&] { return render_.reserve_mesh(); });
}
result<texture> asset_manager::implementation::reserve_texture() noexcept {
  return create<texture>([&] { return render_.reserve_texture(); });
}
result<material> asset_manager::implementation::reserve_material() noexcept {
  return create<material>([&] { return render_.reserve_material(); });
}
result<mesh> asset_manager::implementation::create_mesh(const mesh_view& desc) noexcept {
  return create<mesh>([&] { return render_.create_mesh(desc); });
}
result<texture> asset_manager::implementation::create_texture(const texture_view& desc) noexcept {
  return create<texture>([&] { return render_.create_texture(desc); });
}
result<material>
asset_manager::implementation::create_material(const material_parameters& parameters,
                                               std::array<texture, 5> dependencies) noexcept {
  if (closed_) {
    return error::invalid_state;
  }
  material_data data{.parameters = parameters};
  for (std::size_t i = 0; i < dependencies.size(); ++i) {
    const auto handle = resource_handle(dependencies[i]);
    if (dependencies[i] && find_texture(handle) != dependencies[i]) {
      return error::invalid_argument;
    }
    data.set_texture(i, handle);
  }
  auto output = create<material>([&] { return render_.create_material(data); });
  if (output)
    detail::material_resource_access::set_content(**output, parameters, std::move(dependencies));
  return output;
}
mesh asset_manager::implementation::find_mesh(rid handle) const noexcept {
  if (closed_ || render_.mesh_status(handle) == error::invalid_handle)
    return {};
  const auto found = meshes_.find(handle);
  return found == meshes_.end() ? mesh{} : mesh{found->second.lock()};
}
material asset_manager::implementation::find_material(rid handle) const noexcept {
  if (closed_ || render_.material_status(handle) == error::invalid_handle)
    return {};
  const auto found = materials_.find(handle);
  return found == materials_.end() ? material{} : material{found->second.lock()};
}
std::size_t asset_manager::implementation::drain() noexcept {
  if (closed_)
    return 0;
  const auto count = retirement_.drain();
  std::erase_if(meshes_, [&](const auto& entry) {
    return render_.mesh_status(entry.first) == error::invalid_handle;
  });
  std::erase_if(textures_, [&](const auto& entry) {
    return render_.texture_status(entry.first) == error::invalid_handle;
  });
  std::erase_if(materials_, [&](const auto& entry) {
    return render_.material_status(entry.first) == error::invalid_handle;
  });
  return count;
}
texture asset_manager::implementation::find_texture(rid handle) const noexcept {
  if (closed_ || render_.texture_status(handle) == error::invalid_handle)
    return {};
  const auto found = textures_.find(handle);
  return found == textures_.end() ? texture{} : texture{found->second.lock()};
}
bool asset_manager::implementation::update_mesh(const mesh& target,
                                                std::shared_ptr<const mesh_data> data) noexcept {
  if (closed_ || !target || !data)
    return false;
  const auto handle = resource_handle(target);
  const auto found = meshes_.find(handle);
  if (found == meshes_.end() || found->second.lock().get() != target.get() ||
      !render_.replace_mesh(handle, data))
    return false;
  update_mesh(handle, *data);
  return true;
}
bool asset_manager::implementation::update_texture(
    const texture& target, std::shared_ptr<const texture_data> data) noexcept {
  if (closed_ || !target || !data)
    return false;
  const auto handle = resource_handle(target);
  const auto found = textures_.find(handle);
  if (found == textures_.end() || found->second.lock().get() != target.get() ||
      !render_.replace_texture(handle, data))
    return false;
  update_texture(handle, *data);
  return true;
}
bool asset_manager::implementation::update_material(const material& target,
                                                    std::shared_ptr<const material_data> data,
                                                    std::array<texture, 5> dependencies) noexcept {
  if (closed_ || !target || !data)
    return false;
  const auto handle = resource_handle(target);
  const auto found = materials_.find(handle);
  if (found == materials_.end() || found->second.lock().get() != target.get())
    return false;
  const auto handles = data->texture_handles();
  for (std::size_t i = 0; i < handles.size(); ++i) {
    if (resource_handle(dependencies[i]) != handles[i] ||
        (dependencies[i] && find_texture(handles[i]) != dependencies[i]))
      return false;
  }
  if (!render_.replace_material(handle, data))
    return false;
  update_material(handle, *data, std::move(dependencies));
  return true;
}

result<mesh> asset_manager::reserve_mesh() noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  return impl_->reserve_mesh();
}
mesh asset_manager::find_mesh(rid id) const noexcept {
  return impl_ ? impl_->find_mesh(id) : mesh{};
}

result<texture> asset_manager::reserve_texture() noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  return impl_->reserve_texture();
}
texture asset_manager::find_texture(rid id) const noexcept {
  return impl_ ? impl_->find_texture(id) : texture{};
}

result<material> asset_manager::reserve_material() noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  return impl_->reserve_material();
}
material asset_manager::find_material(rid id) const noexcept {
  return impl_ ? impl_->find_material(id) : material{};
}

result<mesh> asset_manager::create_mesh(const mesh_view& data) noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  return impl_->create_mesh(data);
}
result<texture> asset_manager::create_texture(const texture_view& data) noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  return impl_->create_texture(data);
}
result<material> asset_manager::create_material(const material_parameters& data,
                                                std::array<texture, 5> deps) noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  return impl_->create_material(data, std::move(deps));
}

bool asset_manager::owns(const render_resource_ref& value) const noexcept {
  if (auto asset = value.as<mesh_resource>())
    return owns(asset);
  if (auto asset = value.as<texture_resource>())
    return owns(asset);
  if (auto asset = value.as<material_resource>())
    return owns(asset);
  return false;
}

namespace {
render_resource_ref cached_resource(const resource_cache::entry& entry) noexcept {
  if (auto asset = entry.as<mesh_resource>())
    return asset;
  if (auto asset = entry.as<texture_resource>())
    return asset;
  if (auto asset = entry.as<material_resource>())
    return asset;
  return {};
}
}

result<render_resource_ref>
asset_manager::find_loaded(const render_resource_source& source,
                           texture_prepare_profile profile) const noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  try {
    auto identity = identify(source.uri, static_cast<std::uint32_t>(source.type));
    if (!identity)
      return identity.error();
    auto domain = std::move(identity->source);
    const auto current = cache_
                             .observe(source.uri, cache_type(source.type), domain,
                                      asset_load_parameters({.texture_profile = this->profile()}))
                             .lock();
    if (!current) {
      return engine::error::not_found;
    }
    if (current->type != cache_type(source.type) || current->is_loading) {
      return engine::error::invalid_argument;
    }
    render_resource_ref target;
    target = cached_resource(*current);
    if (!owns(target)) {
      return engine::error::invalid_handle;
    }
    auto status = engine::error::invalid_argument;
    switch (source.type) {
    case render_resource_kind::texture:
      status = impl_->render().texture_status(resource_rid(target));
      break;
    case render_resource_kind::mesh:
      status = impl_->render().mesh_status(resource_rid(target));
      break;
    case render_resource_kind::material:
      status = impl_->render().material_status(resource_rid(target));
      break;
    case render_resource_kind::invalid:
      break;
    }
    if (status != engine::error::success) {
      return status;
    }
    const auto matches_profile = [&](const texture_resource& resource) {
      const auto rid = detail::texture_resource_access::handle(resource);
      const auto* texture = impl_->render().get_texture(rid);
      return texture &&
             (profile.generation == 0U || texture->manifest.empty() || texture->profile == profile);
    };
    if (source.type == render_resource_kind::texture &&
        !matches_profile(*current->as<texture_resource>())) {
      return engine::error::not_found;
    }
    if (source.type == render_resource_kind::material) {
      const auto material = std::static_pointer_cast<const material_resource>(current->resource);
      for (const auto& texture : material->textures()) {
        if (texture && !matches_profile(*texture)) {
          return engine::error::not_found;
        }
      }
    }
    return cached_resource(*current);
  } catch (...) {
    return engine::error::out_of_memory;
  }
}

result<render_resource_ref>
asset_manager::reserve_resource(const render_resource_source& source) noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  try {
    if (engine::validate_uri(source.uri) != engine::error::success) {
      return engine::error::invalid_argument;
    }
    auto identified = identify(source.uri, static_cast<std::uint32_t>(source.type));
    if (!identified)
      return identified.error();
    auto identity = std::move(*identified);
    auto cached = cache_.load(
        source.uri, cache_type(source.type),
        [&]() -> result<std::shared_ptr<const void>> {
          switch (source.type) {
          case render_resource_kind::mesh: {
            auto value = impl_->reserve_mesh();
            if (!value)
              return value.error();
            detail::mesh_resource_access::set_source(**value, identity);
            return std::shared_ptr<const void>(value->shared());
          }
          case render_resource_kind::texture: {
            auto value = impl_->reserve_texture();
            if (!value)
              return value.error();
            detail::texture_resource_access::set_source(**value, identity);
            return std::shared_ptr<const void>(value->shared());
          }
          case render_resource_kind::material: {
            auto value = impl_->reserve_material();
            if (!value)
              return value.error();
            detail::material_resource_access::set_source(**value, identity);
            return std::shared_ptr<const void>(value->shared());
          }
          default:
            return error::invalid_argument;
          }
        },
        identity.source, false, 0, asset_load_parameters({.texture_profile = profile()}));
    if (!cached)
      return cached.error();
    auto resource = cached_resource(**cached);
    if (!owns(resource))
      return error::invalid_handle;
    return resource;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

error asset_manager::implementation::validate_sources(
    asset_manager& assets, std::span<const asset_read_record> sources) noexcept {
  for (const auto& input : sources) {
    auto identity = assets.identify(input.identity.uri, input.identity.type);
    if (!identity || *identity != input.identity)
      return error::invalid_argument;
  }
  return error::success;
}
error asset_manager::implementation::adopt(asset_manager& assets,
                                           std::shared_ptr<const mesh_data> content,
                                           const mesh& target,
                                           std::span<const asset_read_record> sources) noexcept
    try {
  if (!assets.impl_ || assets.render_stopping_)
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  const auto valid = validate_sources(assets, sources);
  if (valid.failed())
    return valid;
  auto prepared = assets.impl_->render().prepare_mesh_data(std::move(content));
  if (!prepared)
    return prepared.error();
  auto data = std::move(*prepared);
  return assets.impl_->update_mesh(target, std::move(data)) ? error::success : error::out_of_memory;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error asset_manager::implementation::adopt(asset_manager& assets,
                                           std::shared_ptr<const texture_data> content,
                                           const texture& target,
                                           std::span<const asset_read_record> sources) noexcept
    try {
  if (!assets.impl_ || assets.render_stopping_)
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  const auto valid = validate_sources(assets, sources);
  if (valid.failed())
    return valid;
  auto prepared = assets.impl_->render().prepare_texture_data(std::move(content));
  if (!prepared)
    return prepared.error();
  auto data = std::move(*prepared);
  return assets.impl_->update_texture(target, std::move(data)) ? error::success
                                                               : error::out_of_memory;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error asset_manager::implementation::adopt(asset_manager& assets,
                                           const material_description& description,
                                           const material& target,
                                           std::array<texture, 5> dependencies) noexcept try {
  if (!assets.impl_ || assets.render_stopping_)
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  material_data value{.parameters = description.parameters};
  for (std::size_t slot = 0; slot < dependencies.size(); ++slot) {
    if (!description.texture_uris[slot].empty() &&
        (!assets.owns(dependencies[slot]) ||
         resource_uri(dependencies[slot]) != description.texture_uris[slot]))
      return error::invalid_argument;
    value.set_texture(slot, resource_handle(dependencies[slot]));
  }
  auto prepared = assets.impl_->render().prepare_material_data(value);
  if (!prepared)
    return prepared.error();
  auto data = std::move(*prepared);
  return assets.impl_->update_material(target, std::move(data), std::move(dependencies))
             ? error::success
             : error::out_of_memory;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

bool asset_manager::owns(const engine::mesh& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().mesh_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::mesh));
  return identity && *identity == value->source();
}

bool asset_manager::owns(const engine::texture& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().texture_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::texture));
  return identity && *identity == value->source();
}

bool asset_manager::owns(const engine::material& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().material_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::material));
  return identity && *identity == value->source();
}

} // namespace gneiss::engine
