// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/resource_bindings.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/resource/material/detail/material_resource_access.hpp"
#include "engine/resource/mesh/detail/mesh_resource_access.hpp"
#include "engine/resource/render_resource_factory.hpp"
#include "engine/resource/texture/detail/texture_resource_access.hpp"
#include <type_traits>
namespace gneiss::engine {
resource_bindings& resource_bindings::of(render_service& render) noexcept {
  return *render.bindings_;
}
template <typename T> std::shared_ptr<T> resource_bindings::make(rid handle) {
  auto finalize = [](T& value, void* owner) noexcept {
    auto* render = static_cast<render_service*>(owner);
    if (!render)
      return;
    if constexpr (std::is_same_v<T, mesh_resource>)
      (void)render->destroy_mesh(detail::mesh_resource_access::handle(value));
    else if constexpr (std::is_same_v<T, texture_resource>)
      (void)render->destroy_texture(detail::texture_resource_access::handle(value));
    else if constexpr (std::is_same_v<T, material_resource>)
      (void)render->destroy_material(detail::material_resource_access::handle(value));
  };
  if constexpr (std::is_same_v<T, mesh_resource>)
    return detail::mesh_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, texture_resource>)
    return detail::texture_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, material_resource>)
    return detail::material_resource_access::make(retirement_, finalize, handle);
}

void resource_bindings::close() noexcept {
  retirement_.close();
  meshes_.clear();
  textures_.clear();
  materials_.clear();
}
mesh resource_bindings::reference_mesh(rid handle) {
  if (render_.mesh_status(handle) == result::invalid_handle)
    return {};
  // 先分配身份槽，避免创建删除器后再分配映射节点失败而允许第二个拥有者。
  const auto [entry, inserted] = meshes_.try_emplace(handle);
  if (!inserted)
    return mesh{entry->second.lock()};
  auto value = make<mesh_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_mesh(handle))
    update_mesh(handle, *data);
  return mesh{std::move(value)};
}
mesh reference_mesh(render_service& render, rid handle) {
  return resource_bindings::of(render).reference_mesh(handle);
}
texture resource_bindings::reference_texture(rid handle) {
  if (render_.texture_status(handle) == result::invalid_handle)
    return {};
  // 先分配身份槽，避免创建删除器后再分配映射节点失败而允许第二个拥有者。
  const auto [entry, inserted] = textures_.try_emplace(handle);
  if (!inserted)
    return texture{entry->second.lock()};
  auto value = make<texture_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_texture(handle))
    update_texture(handle, *data);
  return texture{std::move(value)};
}
texture reference_texture(render_service& render, rid handle) {
  return resource_bindings::of(render).reference_texture(handle);
}
material resource_bindings::reference_material(rid handle) {
  if (render_.material_status(handle) == result::invalid_handle)
    return {};
  // 先分配身份槽，避免创建删除器后再分配映射节点失败而允许第二个拥有者。
  const auto [entry, inserted] = materials_.try_emplace(handle);
  if (!inserted)
    return material{entry->second.lock()};
  auto value = make<material_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_material(handle)) {
    std::array<texture, 5> dependencies;
    if (!prepare_material(handle, *data, dependencies))
      return {};
    update_material(handle, *data, std::move(dependencies));
  }
  return material{std::move(value)};
}
material reference_material(render_service& render, rid handle) {
  return resource_bindings::of(render).reference_material(handle);
}
void resource_bindings::update_mesh(rid handle, const mesh_data& data) noexcept {
  const auto found = meshes_.find(handle);
  if (found != meshes_.end())
    if (auto value = found->second.lock())
      detail::mesh_resource_access::set_metadata(
          *value, mesh_metadata{data.vertices.size(), data.indices.size(), !data.normals.empty(),
                                !data.uv1.empty()});
}
void resource_bindings::update_texture(rid handle, const texture_data& data) noexcept {
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
bool resource_bindings::prepare_material(rid handle, const material_data& data,
                                         std::array<texture, 5>& dependencies) {
  const auto found = materials_.find(handle);
  if (found == materials_.end() || found->second.expired())
    return true;
  const auto handles = data.texture_handles();
  for (std::size_t i = 0; i < handles.size(); ++i) {
    if (handles[i] == null_rid)
      continue;
    dependencies[i] = reference_texture(handles[i]);
    if (!dependencies[i])
      return false;
  }
  return true;
}
void resource_bindings::update_material(rid handle, const material_data& data,
                                        std::array<texture, 5> dependencies) noexcept {
  const auto found = materials_.find(handle);
  if (found != materials_.end())
    if (auto value = found->second.lock()) {
      detail::material_resource_access::set_content(*value, data.parameters,
                                                    std::move(dependencies));
    }
}
result create_texture(render_service& render, const texture_view& desc, texture& output) noexcept {
  output.reset();
  rid handle{};
  const auto status = render.create_texture(desc, &handle);
  if (status.failed())
    return status;
  try {
    output = reference_texture(render, handle);
    if (output)
      return result::success;
    (void)render.destroy_texture(handle);
    return result::invalid_state;
  } catch (const std::bad_alloc&) {
    (void)render.destroy_texture(handle);
    return result::out_of_memory;
  } catch (...) {
    (void)render.destroy_texture(handle);
    return result::internal;
  }
}
} // namespace gneiss::engine
