// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "function/asset/detail/asset_construction.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include "function/asset/resource_cache.hpp"
#include "resource/material/detail/material_resource_access.hpp"
#include "resource/mesh/detail/mesh_resource_access.hpp"
#include "resource/texture/detail/texture_resource_access.hpp"

#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/resource/asset_uri.hpp>
#include <gneiss/engine/resource/mesh/mesh_binary.hpp>
#include <gneiss/engine/function/asset/loaders/texture_decoder.hpp>

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

namespace gneiss::detail {

asset_construction::asset_construction(gneiss::render::service& render)
    : render_(render), retirement_(&render) {}

asset_construction::~asset_construction() { close(); }

template <typename T> std::shared_ptr<T> asset_construction::make(rid handle) {
  auto finalize = [](rid& handle, void* owner) noexcept {
    auto* render = static_cast<gneiss::render::service*>(owner);
    if (!render)
      return;
    if constexpr (std::is_same_v<T, gneiss::mesh>) {
      if (render->mesh_status(handle) != error::invalid_handle)
        (void)render->destroy_mesh(handle);
    } else if constexpr (std::is_same_v<T, gneiss::texture>) {
      if (render->texture_status(handle) != error::invalid_handle)
        (void)render->destroy_texture(handle);
    } else if constexpr (std::is_same_v<T, gneiss::material>) {
      if (render->material_status(handle) != error::invalid_handle)
        (void)render->destroy_material(handle);
    }
  };
  if constexpr (std::is_same_v<T, gneiss::mesh>)
    return detail::mesh_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, gneiss::texture>)
    return detail::texture_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, gneiss::material>)
    return detail::material_resource_access::make(retirement_, finalize, handle);
}

void asset_construction::close() noexcept {
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

ref<const gneiss::mesh> asset_construction::wrap_mesh(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = meshes_.try_emplace(handle).first;
  auto value = make<gneiss::mesh>(handle);
  entry->second = value;
  if (const auto* data = render_.get_mesh(handle))
    update_mesh(handle, *data);
  return ref<const gneiss::mesh>{std::move(value)};
}

ref<const gneiss::texture> asset_construction::wrap_texture(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = textures_.try_emplace(handle).first;
  auto value = make<gneiss::texture>(handle);
  entry->second = value;
  if (const auto* data = render_.get_texture(handle))
    update_texture(handle, *data);
  return ref<const gneiss::texture>{std::move(value)};
}

ref<const gneiss::material> asset_construction::wrap_material(rid handle) {
  const auto entry = materials_.try_emplace(handle).first;
  auto value = make<gneiss::material>(handle);
  entry->second = value;
  return ref<const gneiss::material>{std::move(value)};
}

void asset_construction::update_mesh(rid handle, const mesh_data& data) {
  const auto found = meshes_.find(handle);
  if (found != meshes_.end())
    if (auto value = found->second.lock())
      detail::mesh_resource_access::set_metadata(
          *value, mesh_metadata{data.vertices.size(), data.indices.size(), !data.normals.empty(),
                                !data.uv1.empty()});
}

void asset_construction::update_texture(rid handle, const texture_data& data) {
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

template <typename Ref, typename Create> result<Ref> asset_construction::create(Create create_rid) {
  if (closed_)
    return error::invalid_state;
  auto created = create_rid();
  if (!created)
    return created.error();
  const auto handle = *created;
  auto cleanup = [&] {
    if constexpr (std::is_same_v<Ref, ref<const gneiss::mesh>>) {
      if (render_.mesh_status(handle) != error::invalid_handle)
        (void)render_.destroy_mesh(handle);
      meshes_.erase(handle);
    } else if constexpr (std::is_same_v<Ref, ref<const gneiss::texture>>) {
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
    if constexpr (std::is_same_v<Ref, ref<const gneiss::mesh>>)
      return wrap_mesh(handle);
    else if constexpr (std::is_same_v<Ref, ref<const gneiss::texture>>)
      return wrap_texture(handle);
    else
      return wrap_material(handle);
  } catch (...) {
    cleanup();
    throw;
  }
}

result<ref<const gneiss::mesh>> asset_construction::reserve_mesh() {
  return create<ref<const gneiss::mesh>>([&] { return render_.reserve_mesh(); });
}

result<ref<const gneiss::texture>> asset_construction::reserve_texture() {
  return create<ref<const gneiss::texture>>([&] { return render_.reserve_texture(); });
}

result<ref<const gneiss::material>> asset_construction::reserve_material() {
  return create<ref<const gneiss::material>>([&] { return render_.reserve_material(); });
}
result<ref<const gneiss::mesh>> asset_construction::create_mesh(const mesh_view& desc) {
  if (closed_)
    return error::invalid_state;
  auto data = render_.prepare_mesh_data(desc);
  if (!data)
    return data.error();
  auto output = reserve_mesh();
  if (!output)
    return output.error();
  if (auto status = update_mesh(*output, std::move(*data)); !status)
    return status.error();
  return output;
}

result<ref<const gneiss::texture>> asset_construction::create_texture(const texture_view& desc) {
  if (closed_)
    return error::invalid_state;
  auto data = render_.prepare_texture_data(desc);
  if (!data)
    return data.error();
  auto output = reserve_texture();
  if (!output)
    return output.error();
  if (auto status = update_texture(*output, std::move(*data)); !status)
    return status.error();
  return output;
}

result<ref<const gneiss::material>>
asset_construction::create_material(const material_parameters& parameters,
                                    std::array<ref<const gneiss::texture>, 5> dependencies) {
  auto data = prepare_material(parameters, dependencies);
  if (!data)
    return data.error();
  auto output = reserve_material();
  if (!output)
    return output.error();
  if (auto status = update_material(*output, std::move(*data), std::move(dependencies)); !status)
    return status.error();
  return output;
}

result<std::shared_ptr<const material_data>> asset_construction::prepare_material(
    const material_parameters& parameters,
    const std::array<ref<const gneiss::texture>, 5>& dependencies) {
  if (closed_)
    return error::invalid_state;
  material_data data{.parameters = parameters};
  for (std::size_t slot = 0; slot < dependencies.size(); ++slot) {
    const auto handle = resource_handle(dependencies[slot]);
    if (dependencies[slot] && find_texture(handle) != dependencies[slot])
      return error::invalid_argument;
    data.set_texture(slot, handle);
  }
  return render_.prepare_material_data(data);
}

ref<const gneiss::mesh> asset_construction::find_mesh(rid handle) const noexcept {
  if (closed_ || render_.mesh_status(handle) == error::invalid_handle)
    return {};
  const auto found = meshes_.find(handle);
  return found == meshes_.end() ? ref<const gneiss::mesh>{}
                                : ref<const gneiss::mesh>{found->second.lock()};
}

ref<const gneiss::material> asset_construction::find_material(rid handle) const noexcept {
  if (closed_ || render_.material_status(handle) == error::invalid_handle)
    return {};
  const auto found = materials_.find(handle);
  return found == materials_.end() ? ref<const gneiss::material>{}
                                   : ref<const gneiss::material>{found->second.lock()};
}

std::size_t asset_construction::drain() noexcept {
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

ref<const gneiss::texture> asset_construction::find_texture(rid handle) const noexcept {
  if (closed_ || render_.texture_status(handle) == error::invalid_handle)
    return {};
  const auto found = textures_.find(handle);
  return found == textures_.end() ? ref<const gneiss::texture>{}
                                  : ref<const gneiss::texture>{found->second.lock()};
}

result<void> asset_construction::update_mesh(const ref<const gneiss::mesh>& target,
                                             std::shared_ptr<const mesh_data> data) {
  if (closed_ || !target || !data)
    return error::invalid_argument;
  const auto handle = resource_handle(target);
  const auto found = meshes_.find(handle);
  if (found == meshes_.end() || found->second.lock().get() != target.get())
    return error::invalid_argument;
  if (auto status = render_.replace_mesh(handle, data); !status)
    return status.error();
  update_mesh(handle, *data);
  mesh_resource_access::set_content(*target, ref<const mesh_data>{std::move(data)});
  return {};
}

result<void> asset_construction::update_texture(const ref<const gneiss::texture>& target,
                                                std::shared_ptr<const texture_data> data) {
  if (closed_ || !target || !data)
    return error::invalid_argument;
  const auto handle = resource_handle(target);
  const auto found = textures_.find(handle);
  if (found == textures_.end() || found->second.lock().get() != target.get())
    return error::invalid_argument;
  if (auto status = render_.replace_texture(handle, data); !status)
    return status.error();
  update_texture(handle, *data);
  texture_resource_access::set_content(*target, ref<const texture_data>{std::move(data)});
  return {};
}

result<void>
asset_construction::update_material(const ref<const gneiss::material>& target,
                                    std::shared_ptr<const material_data> data,
                                    std::array<ref<const gneiss::texture>, 5> dependencies) {
  if (closed_ || !target || !data)
    return error::invalid_argument;
  const auto handle = resource_handle(target);
  const auto found = materials_.find(handle);
  if (found == materials_.end() || found->second.lock().get() != target.get())
    return error::invalid_argument;
  const auto handles = data->texture_handles();
  for (std::size_t i = 0; i < handles.size(); ++i) {
    if (resource_handle(dependencies[i]) != handles[i] ||
        (dependencies[i] && find_texture(handles[i]) != dependencies[i]))
      return error::invalid_argument;
  }
  ref<const material_content> content;
  {
    content =
        make_ref<material_content>(material_content{data->parameters, std::move(dependencies)});
  }
  if (auto status = render_.replace_material(handle, data); !status)
    return status.error();
  material_resource_access::set_content(*target, std::move(content));
  return {};
}

error asset_construction::validate_sources(asset_manager& assets,
                                           std::span<const asset_read_record> sources) {
  for (const auto& input : sources) {
    auto identity = assets.identify(input.identity.uri, input.identity.type);
    if (!identity || *identity != input.identity)
      return error::invalid_argument;
  }
  return error::success;
}

} // namespace gneiss::detail
