// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/resource/resource_service.hpp"
#include "engine/core/error.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
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
resource_service::implementation::implementation(render_service& render)
    : render_(render), retirement_(&render) {}
resource_service::implementation::~implementation() { close(); }


template <typename T> std::shared_ptr<T> resource_service::implementation::make(rid handle) {
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

void resource_service::implementation::close() noexcept {
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
mesh resource_service::implementation::wrap_mesh(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = meshes_.try_emplace(handle).first;
  auto value = make<mesh_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_mesh(handle))
    update_mesh(handle, *data);
  return mesh{std::move(value)};
}
texture resource_service::implementation::wrap_texture(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = textures_.try_emplace(handle).first;
  auto value = make<texture_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_texture(handle))
    update_texture(handle, *data);
  return texture{std::move(value)};
}
material resource_service::implementation::wrap_material(rid handle) {
  const auto entry = materials_.try_emplace(handle).first;
  auto value = make<material_resource>(handle);
  entry->second = value;
  return material{std::move(value)};
}
void resource_service::implementation::update_mesh(rid handle, const mesh_data& data) noexcept {
  const auto found = meshes_.find(handle);
  if (found != meshes_.end())
    if (auto value = found->second.lock())
      detail::mesh_resource_access::set_metadata(
          *value, mesh_metadata{data.vertices.size(), data.indices.size(), !data.normals.empty(),
                                !data.uv1.empty()});
}
void resource_service::implementation::update_texture(rid handle,
                                                      const texture_data& data) noexcept {
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
void resource_service::implementation::update_material(
    rid handle, const material_data& data, std::array<texture, 5> dependencies) noexcept {
  const auto found = materials_.find(handle);
  if (found != materials_.end())
    if (auto value = found->second.lock()) {
      detail::material_resource_access::set_content(*value, data.parameters,
                                                    std::move(dependencies));
    }
}
template <typename Ref, typename Create>
error resource_service::implementation::create(Ref& output, Create create_rid) noexcept {
  output.reset();
  if (closed_)
    return error::invalid_state;
  rid handle{};
  const auto status = create_rid(&handle);
  if (status.failed())
    return status;
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
      output = wrap_mesh(handle);
    else if constexpr (std::is_same_v<Ref, texture>)
      output = wrap_texture(handle);
    else
      output = wrap_material(handle);
    return error::success;
  } catch (const std::bad_alloc&) {
    cleanup();
    return error::out_of_memory;
  } catch (...) {
    cleanup();
    return error::internal;
  }
}
error resource_service::implementation::reserve_mesh(mesh& output) noexcept {
  return create(output, [&](rid* handle) { return render_.reserve_mesh(handle); });
}
error resource_service::implementation::reserve_texture(texture& output) noexcept {
  return create(output, [&](rid* handle) { return render_.reserve_texture(handle); });
}
error resource_service::implementation::reserve_material(material& output) noexcept {
  return create(output, [&](rid* handle) { return render_.reserve_material(handle); });
}
error resource_service::implementation::create_mesh(const mesh_view& desc, mesh& output) noexcept {
  return create(output, [&](rid* handle) { return render_.create_mesh(desc, handle); });
}
error resource_service::implementation::create_texture(const texture_view& desc,
                                                       texture& output) noexcept {
  return create(output, [&](rid* handle) { return render_.create_texture(desc, handle); });
}
error resource_service::implementation::create_material(const material_parameters& parameters,
                                                        std::array<texture, 5> dependencies,
                                                        material& output) noexcept {
  if (closed_) {
    output.reset();
    return error::invalid_state;
  }
  material_data data{.parameters = parameters};
  for (std::size_t i = 0; i < dependencies.size(); ++i) {
    const auto handle = resource_handle(dependencies[i]);
    if (dependencies[i] && find_texture(handle) != dependencies[i]) {
      output.reset();
      return error::invalid_argument;
    }
    data.set_texture(i, handle);
  }
  const auto status =
      create(output, [&](rid* handle) { return render_.create_material(data, handle); });
  if (status.ok())
    detail::material_resource_access::set_content(*output, parameters, std::move(dependencies));
  return status;
}
mesh resource_service::implementation::find_mesh(rid handle) const noexcept {
  if (closed_ || render_.mesh_status(handle) == error::invalid_handle)
    return {};
  const auto found = meshes_.find(handle);
  return found == meshes_.end() ? mesh{} : mesh{found->second.lock()};
}
material resource_service::implementation::find_material(rid handle) const noexcept {
  if (closed_ || render_.material_status(handle) == error::invalid_handle)
    return {};
  const auto found = materials_.find(handle);
  return found == materials_.end() ? material{} : material{found->second.lock()};
}
std::size_t resource_service::implementation::drain() noexcept {
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
texture resource_service::implementation::find_texture(rid handle) const noexcept {
  if (closed_ || render_.texture_status(handle) == error::invalid_handle)
    return {};
  const auto found = textures_.find(handle);
  return found == textures_.end() ? texture{} : texture{found->second.lock()};
}
bool resource_service::implementation::update_mesh(const mesh& target,
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
bool resource_service::implementation::update_texture(
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
bool resource_service::implementation::update_material(
    const material& target, std::shared_ptr<const material_data> data,
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

resource_service::resource_service(asset_manager& assets) : assets_(assets) {}
resource_service::~resource_service() { stop(); }
result<asset_identity> resource_service::identify(std::string_view uri,
                                                  std::uint32_t type) const noexcept {
  return assets_.identify(uri, type);
}

error resource_service::attach_render(render_service& render, texture_prepare_profile profile) {
  if (render_stopping_)
    return error::invalid_state;
  if (impl_)
    return &impl_->render() == &render ? error::success : error::invalid_state;
  impl_ = std::make_unique<implementation>(render);
  impl_->default_profile = profile;
  return error::success;
}
error resource_service::configure_loading(task_executor& executor, texture_prepare_profile profile,
                                          std::shared_ptr<progress_notification> notification) {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  if (impl_->loading_configured)
    return error::success;
  assets_.attach_task_executor(executor);
  impl_->default_profile = profile;
  impl_->notification = std::move(notification);
  impl_->loading_configured = true;
  return error::success;
}

void resource_service::advance() {
  if (!impl_)
    return;
  (void)impl_->drain();
  impl_->advance(*this);
}
void resource_service::stop() noexcept {
  render_stopping_ = true;
  if (impl_)
    impl_->stop_loading();
  collect_unused();
  if (impl_)
    impl_->close();
}
void resource_service::request_stop() noexcept {
  render_stopping_ = true;
  if (impl_)
    impl_->stop_loading();
}
bool resource_service::stopped() const noexcept {
  return !impl_ || (render_stopping_ && impl_->pending.empty());
}

result<mesh> resource_service::reserve_mesh() noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  mesh value;
  const auto status = impl_->reserve_mesh(value);
  if (status.failed())
    return status;
  return value;
}
mesh resource_service::find_mesh(rid id) const noexcept {
  return impl_ ? impl_->find_mesh(id) : mesh{};
}

result<texture> resource_service::reserve_texture() noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  texture value;
  const auto status = impl_->reserve_texture(value);
  if (status.failed())
    return status;
  return value;
}
texture resource_service::find_texture(rid id) const noexcept {
  return impl_ ? impl_->find_texture(id) : texture{};
}

result<material> resource_service::reserve_material() noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  material value;
  const auto status = impl_->reserve_material(value);
  if (status.failed())
    return status;
  return value;
}
material resource_service::find_material(rid id) const noexcept {
  return impl_ ? impl_->find_material(id) : material{};
}

result<mesh> resource_service::create_mesh(const mesh_view& data) noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  mesh out;
  const auto status = impl_->create_mesh(data, out);
  if (status.failed())
    return status;
  return out;
}
result<texture> resource_service::create_texture(const texture_view& data) noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  texture out;
  const auto status = impl_->create_texture(data, out);
  if (status.failed())
    return status;
  return out;
}
result<material> resource_service::create_material(const material_parameters& data,
                                                   std::array<texture, 5> deps) noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  material out;
  const auto status = impl_->create_material(data, std::move(deps), out);
  if (status.failed())
    return status;
  return out;
}

texture_prepare_profile resource_service::profile() const noexcept {
  return impl_ ? impl_->default_profile : texture_prepare_profile{};
}

void resource_service::collect_unused() noexcept {
  if (!impl_) {
    cache_.release_unused();
    return;
  }
  // 析构材质才会释放其纹理依赖，消费后重新扫描统一缓存。
  do {
    cache_.release_unused();
  } while (impl_->drain() != 0U);
}

bool resource_service::owns(const render_asset_ref& lease) const noexcept {
  if (!impl_)
    return false;
  if (!lease || !lease->resource)
    return false;
  auto identity = identify(lease->uri, static_cast<std::uint32_t>(asset_kind(lease)));
  if (!identity || identity->source != lease->source)
    return false;
  switch (asset_kind(lease)) {
  case asset_type::mesh:
    return impl_->render().mesh_status(asset_rid(lease)) != engine::error::invalid_handle;
  case asset_type::material:
    return impl_->render().material_status(asset_rid(lease)) != engine::error::invalid_handle;
  case asset_type::texture:
    return impl_->render().texture_status(asset_rid(lease)) != engine::error::invalid_handle;
  default:
    return false;
  }
}

result<render_asset_ref> resource_service::find(const asset_source& source,
                                                texture_prepare_profile profile) const noexcept {
  if (!impl_ || render_stopping_)
    return error::invalid_state;
  try {
    auto identity = identify(source.uri, static_cast<std::uint32_t>(source.type));
    if (!identity)
      return identity.error();
    auto domain = std::move(identity->source);
    const auto current = cache_.observe(source.uri, cache_type(source.type), domain).lock();
    if (!current) {
      return engine::error::not_found;
    }
    if (current->type != cache_type(source.type) || current->is_loading) {
      return engine::error::invalid_argument;
    }
    render_asset_ref target;
    target = render_asset_ref{current};
    if (!owns(target)) {
      return engine::error::invalid_handle;
    }
    auto status = engine::error::invalid_argument;
    switch (source.type) {
    case asset_type::texture:
      status = impl_->render().texture_status(asset_rid(target));
      break;
    case asset_type::mesh:
      status = impl_->render().mesh_status(asset_rid(target));
      break;
    case asset_type::material:
      status = impl_->render().material_status(asset_rid(target));
      break;
    case asset_type::invalid:
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
    if (source.type == asset_type::texture && !matches_profile(*current->as<texture_resource>())) {
      return engine::error::not_found;
    }
    if (source.type == asset_type::material) {
      const auto material = std::static_pointer_cast<const material_resource>(current->resource);
      for (const auto& texture : material->textures()) {
        if (texture && !matches_profile(*texture)) {
          return engine::error::not_found;
        }
      }
    }
    return render_asset_ref{current};
  } catch (...) {
    return engine::error::out_of_memory;
  }
}

result<render_asset_ref> resource_service::reserve_asset(const asset_source& source) noexcept {
  render_asset_ref output;
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
    const auto result = cache_.load(
        source.uri, cache_type(source.type),
        [&](std::shared_ptr<const void>& resource) {
          auto result = engine::error::invalid_argument;
          switch (source.type) {
          case asset_type::mesh: {
            engine::mesh value;
            result = impl_->reserve_mesh(value);
            if (result.ok()) {
              detail::mesh_resource_access::set_source(*value, identity);
              resource = value.shared();
            }
            break;
          }
          case asset_type::texture: {
            engine::texture value;
            result = impl_->reserve_texture(value);
            if (result.ok()) {
              detail::texture_resource_access::set_source(*value, identity);
              resource = value.shared();
            }
            break;
          }
          case asset_type::material: {
            engine::material value;
            result = impl_->reserve_material(value);
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
    if (result == engine::error::success && !owns(output)) {
      output.reset();
      return engine::error::invalid_handle;
    }
    if (result.failed())
      return result;
    return output;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

error resource_service::implementation::validate_sources(
    resource_service& assets, std::span<const asset_data_source> sources) noexcept {
  for (const auto& input : sources) {
    auto identity = assets.identify(input.identity.uri, input.identity.type);
    if (!identity || *identity != input.identity)
      return error::invalid_argument;
  }
  return error::success;
}
error resource_service::implementation::adopt(resource_service& assets, mesh_data content,
                                              const mesh& target,
                                              std::span<const asset_data_source> sources) noexcept
    try {
  if (!assets.impl_ || assets.render_stopping_)
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  const auto valid = validate_sources(assets, sources);
  if (valid.failed())
    return valid;
  std::shared_ptr<const mesh_data> data;
  const auto status = assets.impl_->render().prepare_mesh_data(
      std::make_shared<const mesh_data>(std::move(content)), data);
  if (status.failed())
    return status;
  return assets.impl_->update_mesh(target, std::move(data)) ? error::success : error::out_of_memory;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error resource_service::implementation::adopt(resource_service& assets, texture_data content,
                                              const texture& target,
                                              std::span<const asset_data_source> sources) noexcept
    try {
  if (!assets.impl_ || assets.render_stopping_)
    return error::invalid_state;
  if (!assets.owns(target))
    return error::invalid_argument;
  const auto valid = validate_sources(assets, sources);
  if (valid.failed())
    return valid;
  std::shared_ptr<const texture_data> data;
  const auto status = assets.impl_->render().prepare_texture_data(std::move(content), data);
  if (status.failed())
    return status;
  return assets.impl_->update_texture(target, std::move(data)) ? error::success
                                                               : error::out_of_memory;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error resource_service::implementation::adopt(resource_service& assets,
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
         asset_uri(dependencies[slot]) != description.texture_uris[slot]))
      return error::invalid_argument;
    value.set_texture(slot, resource_handle(dependencies[slot]));
  }
  std::shared_ptr<const material_data> data;
  const auto status = assets.impl_->render().prepare_material_data(value, data);
  if (status.failed())
    return status;
  return assets.impl_->update_material(target, std::move(data), std::move(dependencies))
             ? error::success
             : error::out_of_memory;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

bool resource_service::owns(const engine::mesh& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().mesh_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity = identify(value->source().uri, static_cast<std::uint32_t>(asset_type::mesh));
  return identity && *identity == value->source();
}

bool resource_service::owns(const engine::texture& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().texture_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity = identify(value->source().uri, static_cast<std::uint32_t>(asset_type::texture));
  return identity && *identity == value->source();
}

bool resource_service::owns(const engine::material& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().material_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity = identify(value->source().uri, static_cast<std::uint32_t>(asset_type::material));
  return identity && *identity == value->source();
}

} // namespace gneiss::engine
