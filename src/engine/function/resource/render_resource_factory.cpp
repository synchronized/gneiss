// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/resource/render_resource_factory.hpp"
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
  mesh wrap_mesh(rid handle);
  texture wrap_texture(rid handle);
  material wrap_material(rid handle);
  void update_mesh(rid handle, const mesh_data& data) noexcept;
  void update_texture(rid handle, const texture_data& data) noexcept;
  void update_material(rid handle, const material_data& data,
                       std::array<texture, 5> dependencies) noexcept;
  std::size_t drain() noexcept;
  bool closed_{};
  void close() noexcept;

  template <typename T> std::shared_ptr<T> make(rid handle);
  template <typename Ref, typename Create> result create(Ref& output, Create create_rid) noexcept;
  render_service& render_;
  resource_retirement_queue retirement_;
  // 仅索引本工厂创建的对象；查询只锁定既有控制块，不接管外部 RID。
  std::unordered_map<rid, std::weak_ptr<const mesh_resource>> meshes_;
  std::unordered_map<rid, std::weak_ptr<const texture_resource>> textures_;
  std::unordered_map<rid, std::weak_ptr<const material_resource>> materials_;
};

template <typename T> std::shared_ptr<T> render_resource_factory::implementation::make(rid handle) {
  auto finalize = [](T& value, void* owner) noexcept {
    auto* render = static_cast<render_service*>(owner);
    if (!render)
      return;
    if constexpr (std::is_same_v<T, mesh_resource>) {
      const auto handle = detail::mesh_resource_access::handle(value);
      if (render->mesh_status(handle) != result::invalid_handle)
        (void)render->destroy_mesh(handle);
    } else if constexpr (std::is_same_v<T, texture_resource>) {
      const auto handle = detail::texture_resource_access::handle(value);
      if (render->texture_status(handle) != result::invalid_handle)
        (void)render->destroy_texture(handle);
    } else if constexpr (std::is_same_v<T, material_resource>) {
      const auto handle = detail::material_resource_access::handle(value);
      if (render->material_status(handle) != result::invalid_handle)
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

void render_resource_factory::implementation::close() noexcept {
  if (closed_)
    return;
  closed_ = true;
  retirement_.close();
  for (const auto& [handle, value] : meshes_)
    if (render_.mesh_status(handle) != result::invalid_handle)
      (void)render_.destroy_mesh(handle);
  for (const auto& [handle, value] : textures_)
    if (render_.texture_status(handle) != result::invalid_handle)
      (void)render_.destroy_texture(handle);
  for (const auto& [handle, value] : materials_)
    if (render_.material_status(handle) != result::invalid_handle)
      (void)render_.destroy_material(handle);
  meshes_.clear();
  textures_.clear();
  materials_.clear();
}
mesh render_resource_factory::implementation::wrap_mesh(rid handle) {
  // 这里只接收本工厂刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = meshes_.try_emplace(handle).first;
  auto value = make<mesh_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_mesh(handle))
    update_mesh(handle, *data);
  return mesh{std::move(value)};
}
texture render_resource_factory::implementation::wrap_texture(rid handle) {
  // 这里只接收本工厂刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = textures_.try_emplace(handle).first;
  auto value = make<texture_resource>(handle);
  entry->second = value;
  if (const auto* data = render_.get_texture(handle))
    update_texture(handle, *data);
  return texture{std::move(value)};
}
material render_resource_factory::implementation::wrap_material(rid handle) {
  const auto entry = materials_.try_emplace(handle).first;
  auto value = make<material_resource>(handle);
  entry->second = value;
  return material{std::move(value)};
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
void render_resource_factory::implementation::update_material(
    rid handle, const material_data& data, std::array<texture, 5> dependencies) noexcept {
  const auto found = materials_.find(handle);
  if (found != materials_.end())
    if (auto value = found->second.lock()) {
      detail::material_resource_access::set_content(*value, data.parameters,
                                                    std::move(dependencies));
    }
}
template <typename Ref, typename Create>
result render_resource_factory::implementation::create(Ref& output, Create create_rid) noexcept {
  output.reset();
  if (closed_)
    return result::invalid_state;
  rid handle{};
  const auto status = create_rid(&handle);
  if (status.failed())
    return status;
  auto cleanup = [&] {
    if constexpr (std::is_same_v<Ref, mesh>) {
      if (render_.mesh_status(handle) != result::invalid_handle)
        (void)render_.destroy_mesh(handle);
      meshes_.erase(handle);
    } else if constexpr (std::is_same_v<Ref, texture>) {
      if (render_.texture_status(handle) != result::invalid_handle)
        (void)render_.destroy_texture(handle);
      textures_.erase(handle);
    } else {
      if (render_.material_status(handle) != result::invalid_handle)
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
    return result::success;
  } catch (const std::bad_alloc&) {
    cleanup();
    return result::out_of_memory;
  } catch (...) {
    cleanup();
    return result::internal;
  }
}
result render_resource_factory::reserve_mesh(mesh& output) noexcept {
  return impl_->create(output, [&](rid* handle) { return impl_->render_.reserve_mesh(handle); });
}
result render_resource_factory::reserve_texture(texture& output) noexcept {
  return impl_->create(output, [&](rid* handle) { return impl_->render_.reserve_texture(handle); });
}
result render_resource_factory::reserve_material(material& output) noexcept {
  return impl_->create(output,
                       [&](rid* handle) { return impl_->render_.reserve_material(handle); });
}
result render_resource_factory::create_mesh(const mesh_view& desc, mesh& output) noexcept {
  return impl_->create(output,
                       [&](rid* handle) { return impl_->render_.create_mesh(desc, handle); });
}
result render_resource_factory::create_texture(const texture_view& desc, texture& output) noexcept {
  return impl_->create(output,
                       [&](rid* handle) { return impl_->render_.create_texture(desc, handle); });
}
result render_resource_factory::create_material(const material_parameters& parameters,
                                                std::array<texture, 5> dependencies,
                                                material& output) noexcept {
  if (impl_->closed_) {
    output.reset();
    return result::invalid_state;
  }
  material_data data{.parameters = parameters};
  for (std::size_t i = 0; i < dependencies.size(); ++i) {
    const auto handle = resource_handle(dependencies[i]);
    if (dependencies[i] && find_texture(handle) != dependencies[i]) {
      output.reset();
      return result::invalid_argument;
    }
    data.set_texture(i, handle);
  }
  const auto status = impl_->create(
      output, [&](rid* handle) { return impl_->render_.create_material(data, handle); });
  if (status.ok())
    detail::material_resource_access::set_content(*output, parameters, std::move(dependencies));
  return status;
}
mesh render_resource_factory::find_mesh(rid handle) const noexcept {
  if (impl_->closed_ || impl_->render_.mesh_status(handle) == result::invalid_handle)
    return {};
  const auto found = impl_->meshes_.find(handle);
  return found == impl_->meshes_.end() ? mesh{} : mesh{found->second.lock()};
}
material render_resource_factory::find_material(rid handle) const noexcept {
  if (impl_->closed_ || impl_->render_.material_status(handle) == result::invalid_handle)
    return {};
  const auto found = impl_->materials_.find(handle);
  return found == impl_->materials_.end() ? material{} : material{found->second.lock()};
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
