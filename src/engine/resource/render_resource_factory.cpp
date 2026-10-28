// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/resource/render_resource_factory.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/resource/material/detail/material_resource_access.hpp"
#include "engine/resource/mesh/detail/mesh_resource_access.hpp"
#include "engine/resource/resource_retirement_queue.hpp"
#include "engine/resource/texture/detail/texture_resource_access.hpp"
#include <type_traits>
#include <unordered_map>
namespace gneiss::engine {
struct render_resource_factory::implementation final {
public:
  explicit implementation(render_service& render) : render_(render), retirement_(&render) {}
  mesh reference_mesh(rid handle);
  texture reference_texture(rid handle);
  material reference_material(rid handle);
  void update_mesh(rid handle, const mesh_data& data) noexcept;
  void update_texture(rid handle, const texture_data& data) noexcept;
  bool prepare_material(const material_data& data, std::array<texture, 5>& dependencies);
  void update_material(rid handle, const material_data& data,
                       std::array<texture, 5> dependencies) noexcept;
  std::size_t drain() noexcept;
  bool closed_{};
  void close() noexcept;

  template <typename T> std::shared_ptr<T> make(rid handle);
  render_service& render_;
  resource_retirement_queue retirement_;
  // 存在但 weak 已过期意味着等待销毁，禁止为同一个 RID 复活第二个拥有者。
  std::unordered_map<rid, std::weak_ptr<const mesh_resource>> meshes_;
  std::unordered_map<rid, std::weak_ptr<const texture_resource>> textures_;
  std::unordered_map<rid, std::weak_ptr<const material_resource>> materials_;
};

template <typename T> std::shared_ptr<T> render_resource_factory::implementation::make(rid handle) {
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

void render_resource_factory::implementation::close() noexcept {
  if (closed_)
    return;
  closed_ = true;
  retirement_.close();
  for (const auto& [handle, value] : meshes_)
    (void)render_.destroy_mesh(handle);
  for (const auto& [handle, value] : textures_)
    (void)render_.destroy_texture(handle);
  for (const auto& [handle, value] : materials_)
    (void)render_.destroy_material(handle);
  meshes_.clear();
  textures_.clear();
  materials_.clear();
}
mesh render_resource_factory::implementation::reference_mesh(rid handle) {
  if (closed_ || render_.mesh_status(handle) == result::invalid_handle)
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
mesh reference_mesh(render_resource_factory& factory, rid handle) {
  return factory.reference_mesh(handle);
}
texture render_resource_factory::implementation::reference_texture(rid handle) {
  if (closed_ || render_.texture_status(handle) == result::invalid_handle)
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
texture reference_texture(render_resource_factory& factory, rid handle) {
  return factory.reference_texture(handle);
}
material render_resource_factory::implementation::reference_material(rid handle) {
  if (closed_ || render_.material_status(handle) == result::invalid_handle)
    return {};
  if (const auto found = materials_.find(handle); found != materials_.end())
    return material{found->second.lock()};
  const auto* data = render_.get_material(handle);
  std::array<texture, 5> dependencies;
  // 接管旧 RID 也只使用已有纹理拥有者，不递归创建资源包装。
  if (data && !prepare_material(*data, dependencies))
    return {};
  const auto entry = materials_.try_emplace(handle).first;
  auto value = make<material_resource>(handle);
  entry->second = value;
  if (data)
    detail::material_resource_access::set_content(*value, data->parameters,
                                                  std::move(dependencies));
  return material{std::move(value)};
}
material reference_material(render_resource_factory& factory, rid handle) {
  return factory.reference_material(handle);
}
void render_resource_factory::implementation::update_mesh(rid handle,
                                                          const mesh_data& data) noexcept {
  const auto found = meshes_.find(handle);
  if (found != meshes_.end())
    if (auto value = found->second.lock())
      detail::mesh_resource_access::set_metadata(
          *value, mesh_metadata{data.vertices.size(), data.indices.size(), !data.normals.empty(),
                                !data.uv1.empty()});
}
void render_resource_factory::implementation::update_texture(rid handle,
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
bool render_resource_factory::implementation::prepare_material(
    const material_data& data, std::array<texture, 5>& dependencies) {
  const auto handles = data.texture_handles();
  for (std::size_t i = 0; i < handles.size(); ++i) {
    if (handles[i] == null_rid)
      continue;
    const auto found = textures_.find(handles[i]);
    if (found == textures_.end() || render_.texture_status(handles[i]) == result::invalid_handle)
      return false;
    dependencies[i] = texture{found->second.lock()};
    if (!dependencies[i])
      return false;
  }
  return true;
}
void render_resource_factory::implementation::update_material(
    rid handle, const material_data& data, std::array<texture, 5> dependencies) noexcept {
  const auto found = materials_.find(handle);
  if (found != materials_.end())
    if (auto value = found->second.lock()) {
      detail::material_resource_access::set_content(*value, data.parameters,
                                                    std::move(dependencies));
    }
}
result render_resource_factory::create_texture(const texture_view& desc, texture& output) noexcept {
  auto& render = impl_->render_;
  if (impl_->closed_) {
    output.reset();
    return result::invalid_state;
  }
  output.reset();
  rid handle{};
  const auto status = render.create_texture(desc, &handle);
  if (status.failed())
    return status;
  try {
    output = reference_texture(handle);
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
render_resource_factory::render_resource_factory(render_service& render)
    : impl_(std::make_unique<implementation>(render)) {}
render_resource_factory::~render_resource_factory() { close(); }
render_service& render_resource_factory::render() const noexcept { return impl_->render_; }
void render_resource_factory::close() noexcept { impl_->close(); }
std::size_t render_resource_factory::implementation::drain() noexcept {
  if (closed_)
    return 0;
  const auto count = retirement_.drain();
  std::erase_if(meshes_, [&](const auto& entry) {
    return render_.mesh_status(entry.first) == result::invalid_handle;
  });
  std::erase_if(textures_, [&](const auto& entry) {
    return render_.texture_status(entry.first) == result::invalid_handle;
  });
  std::erase_if(materials_, [&](const auto& entry) {
    return render_.material_status(entry.first) == result::invalid_handle;
  });
  return count;
}
std::size_t render_resource_factory::drain() noexcept { return impl_->drain(); }
mesh render_resource_factory::reference_mesh(rid handle) { return impl_->reference_mesh(handle); }
texture render_resource_factory::reference_texture(rid handle) {
  return impl_->reference_texture(handle);
}
material render_resource_factory::reference_material(rid handle) {
  return impl_->reference_material(handle);
}
texture render_resource_factory::find_texture(rid handle) const noexcept {
  if (impl_->closed_ || impl_->render_.texture_status(handle) == result::invalid_handle)
    return {};
  const auto found = impl_->textures_.find(handle);
  return found == impl_->textures_.end() ? texture{} : texture{found->second.lock()};
}
bool render_resource_factory::update_mesh(const mesh& target,
                                          std::shared_ptr<const mesh_data> data) noexcept {
  if (impl_->closed_ || !target || !data)
    return false;
  const auto handle = resource_handle(target);
  const auto found = impl_->meshes_.find(handle);
  if (found == impl_->meshes_.end() || found->second.lock().get() != target.get() ||
      !impl_->render_.replace_mesh(handle, data))
    return false;
  impl_->update_mesh(handle, *data);
  return true;
}
bool render_resource_factory::update_texture(const texture& target,
                                             std::shared_ptr<const texture_data> data) noexcept {
  if (impl_->closed_ || !target || !data)
    return false;
  const auto handle = resource_handle(target);
  const auto found = impl_->textures_.find(handle);
  if (found == impl_->textures_.end() || found->second.lock().get() != target.get() ||
      !impl_->render_.replace_texture(handle, data))
    return false;
  impl_->update_texture(handle, *data);
  return true;
}
bool render_resource_factory::update_material(const material& target,
                                              std::shared_ptr<const material_data> data,
                                              std::array<texture, 5> dependencies) noexcept {
  if (impl_->closed_ || !target || !data)
    return false;
  const auto handle = resource_handle(target);
  const auto found = impl_->materials_.find(handle);
  if (found == impl_->materials_.end() || found->second.lock().get() != target.get())
    return false;
  const auto handles = data->texture_handles();
  for (std::size_t i = 0; i < handles.size(); ++i) {
    if (resource_handle(dependencies[i]) != handles[i] ||
        (dependencies[i] && find_texture(handles[i]) != dependencies[i]))
      return false;
  }
  if (!impl_->render_.replace_material(handle, data))
    return false;
  impl_->update_material(handle, *data, std::move(dependencies));
  return true;
}
result create_texture(render_resource_factory& factory, const texture_view& desc,
                      texture& output) noexcept {
  return factory.create_texture(desc, output);
}
} // namespace gneiss::engine
