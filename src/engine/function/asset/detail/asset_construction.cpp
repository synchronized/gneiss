// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/function/asset/detail/asset_construction.hpp"
#include "engine/core/error.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/asset/asset_reference_context.hpp"
#include "engine/function/asset/resource_cache.hpp"
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

namespace gneiss::engine::detail {

asset_construction::asset_construction(render_service& render)
    : render_(render), retirement_(&render) {}

asset_construction::~asset_construction() { close(); }

template <typename T> std::shared_ptr<T> asset_construction::make(rid handle) {
  auto finalize = [](T& value, void* owner) noexcept {
    auto* render = static_cast<render_service*>(owner);
    if (!render)
      return;
    if constexpr (std::is_same_v<T, engine::mesh>) {
      const auto handle = detail::mesh_resource_access::handle(value);
      if (render->mesh_status(handle) != error::invalid_handle)
        (void)render->destroy_mesh(handle);
    } else if constexpr (std::is_same_v<T, engine::texture>) {
      const auto handle = detail::texture_resource_access::handle(value);
      if (render->texture_status(handle) != error::invalid_handle)
        (void)render->destroy_texture(handle);
    } else if constexpr (std::is_same_v<T, engine::material>) {
      const auto handle = detail::material_resource_access::handle(value);
      if (render->material_status(handle) != error::invalid_handle)
        (void)render->destroy_material(handle);
    }
  };
  if constexpr (std::is_same_v<T, engine::mesh>)
    return detail::mesh_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, engine::texture>)
    return detail::texture_resource_access::make(retirement_, finalize, handle);
  else if constexpr (std::is_same_v<T, engine::material>)
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

ref<const engine::mesh> asset_construction::wrap_mesh(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = meshes_.try_emplace(handle).first;
  auto value = make<engine::mesh>(handle);
  entry->second = value;
  if (const auto* data = render_.get_mesh(handle))
    update_mesh(handle, *data);
  return ref<const engine::mesh>{std::move(value)};
}

ref<const engine::texture> asset_construction::wrap_texture(rid handle) {
  // 这里只接收本服务刚创建的 RID；先分配索引，再构造拥有者。
  const auto entry = textures_.try_emplace(handle).first;
  auto value = make<engine::texture>(handle);
  entry->second = value;
  if (const auto* data = render_.get_texture(handle))
    update_texture(handle, *data);
  return ref<const engine::texture>{std::move(value)};
}

ref<const engine::material> asset_construction::wrap_material(rid handle) {
  const auto entry = materials_.try_emplace(handle).first;
  auto value = make<engine::material>(handle);
  entry->second = value;
  return ref<const engine::material>{std::move(value)};
}

void asset_construction::update_mesh(rid handle, const mesh_data& data) noexcept {
  const auto found = meshes_.find(handle);
  if (found != meshes_.end())
    if (auto value = found->second.lock())
      detail::mesh_resource_access::set_metadata(
          *value, mesh_metadata{data.vertices.size(), data.indices.size(), !data.normals.empty(),
                                !data.uv1.empty()});
}

void asset_construction::update_texture(rid handle, const texture_data& data) noexcept {
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

void asset_construction::update_material(
    rid handle, const material_data& data,
    std::array<ref<const engine::texture>, 5> dependencies) noexcept {
  const auto found = materials_.find(handle);
  if (found != materials_.end())
    if (auto value = found->second.lock()) {
      detail::material_resource_access::set_content(*value, data.parameters,
                                                    std::move(dependencies));
    }
}

template <typename Ref, typename Create>
result<Ref> asset_construction::create(Create create_rid) noexcept {
  if (closed_)
    return error::invalid_state;
  auto created = create_rid();
  if (!created)
    return created.error();
  const auto handle = *created;
  auto cleanup = [&] {
    if constexpr (std::is_same_v<Ref, ref<const engine::mesh>>) {
      if (render_.mesh_status(handle) != error::invalid_handle)
        (void)render_.destroy_mesh(handle);
      meshes_.erase(handle);
    } else if constexpr (std::is_same_v<Ref, ref<const engine::texture>>) {
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
    if constexpr (std::is_same_v<Ref, ref<const engine::mesh>>)
      return wrap_mesh(handle);
    else if constexpr (std::is_same_v<Ref, ref<const engine::texture>>)
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

result<ref<const engine::mesh>> asset_construction::reserve_mesh() noexcept {
  return create<ref<const engine::mesh>>([&] { return render_.reserve_mesh(); });
}

result<ref<const engine::texture>> asset_construction::reserve_texture() noexcept {
  return create<ref<const engine::texture>>([&] { return render_.reserve_texture(); });
}

result<ref<const engine::material>> asset_construction::reserve_material() noexcept {
  return create<ref<const engine::material>>([&] { return render_.reserve_material(); });
}
result<ref<const engine::mesh>> asset_construction::create_mesh(const mesh_view& desc) noexcept {
  return create<ref<const engine::mesh>>([&] { return render_.create_mesh(desc); });
}

result<ref<const engine::texture>>
asset_construction::create_texture(const texture_view& desc) noexcept {
  return create<ref<const engine::texture>>([&] { return render_.create_texture(desc); });
}

result<ref<const engine::material>> asset_construction::create_material(
    const material_parameters& parameters,
    std::array<ref<const engine::texture>, 5> dependencies) noexcept {
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
  auto output = create<ref<const engine::material>>([&] { return render_.create_material(data); });
  if (output)
    detail::material_resource_access::set_content(**output, parameters, std::move(dependencies));
  return output;
}

ref<const engine::mesh> asset_construction::find_mesh(rid handle) const noexcept {
  if (closed_ || render_.mesh_status(handle) == error::invalid_handle)
    return {};
  const auto found = meshes_.find(handle);
  return found == meshes_.end() ? ref<const engine::mesh>{}
                                : ref<const engine::mesh>{found->second.lock()};
}

ref<const engine::material> asset_construction::find_material(rid handle) const noexcept {
  if (closed_ || render_.material_status(handle) == error::invalid_handle)
    return {};
  const auto found = materials_.find(handle);
  return found == materials_.end() ? ref<const engine::material>{}
                                   : ref<const engine::material>{found->second.lock()};
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

ref<const engine::texture> asset_construction::find_texture(rid handle) const noexcept {
  if (closed_ || render_.texture_status(handle) == error::invalid_handle)
    return {};
  const auto found = textures_.find(handle);
  return found == textures_.end() ? ref<const engine::texture>{}
                                  : ref<const engine::texture>{found->second.lock()};
}

bool asset_construction::update_mesh(const ref<const engine::mesh>& target,
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

bool asset_construction::update_texture(const ref<const engine::texture>& target,
                                        std::shared_ptr<const texture_data> data) noexcept {
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

bool asset_construction::update_material(
    const ref<const engine::material>& target, std::shared_ptr<const material_data> data,
    std::array<ref<const engine::texture>, 5> dependencies) noexcept {
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

error asset_construction::validate_sources(asset_manager& assets,
                                           std::span<const asset_read_record> sources) noexcept {
  for (const auto& input : sources) {
    auto identity = assets.identify(input.identity.uri, input.identity.type);
    if (!identity || *identity != input.identity)
      return error::invalid_argument;
  }
  return error::success;
}

void asset_construction::stop_loading() noexcept {
  for (auto& work : pending) {
    work->observer->status = error::invalid_state;
    if (auto entry = work->entry.lock(); entry && entry->loading == work)
      entry->loading.reset();
  }
  pending.clear();
}

void asset_construction::advance(asset_manager& service) {
  // 依赖请求可能追加到 pending；每次只处理开始时已有的工作。
  const auto count = pending.size();
  for (std::size_t i = 0; i < count; ++i) {
    auto work = pending[i];
    auto& state = *work->observer;
    if (work->observer.use_count() == 1 || state.status != error::not_ready)
      continue;
    try {
      if (!work->description) {
        serialization::diagnostic diagnostic;
        const auto status = std::visit(
            [&](auto& request) -> error {
              using T = std::decay_t<decltype(request)>;
              auto loaded = request.take();
              if (!loaded) {
                auto failure = std::move(loaded).error();
                diagnostic = {failure.byte_offset, std::move(failure.path),
                              std::move(failure.message), failure.error};
                return failure.error;
              }
              state.source_bytes = request.source_bytes();
              if constexpr (std::is_same_v<T, asset_request<mesh_data>>) {
                state.metrics = request.metrics();
                return adopt(service, loaded->shared(), resource_cast<engine::mesh>(state.target),
                             state.metrics.sources);
              } else if constexpr (std::is_same_v<T, asset_request<texture_data>>) {
                state.metrics = request.metrics();
                return adopt(service, loaded->shared(),
                             resource_cast<engine::texture>(state.target), state.metrics.sources);
              } else {
                auto description = std::move(*loaded);
                auto bytes = sizeof(material_data);
                for (const auto& uri : description->texture_uris) {
                  if (uri.size() > std::numeric_limits<std::size_t>::max() - bytes)
                    return error::out_of_memory;
                  bytes += uri.size();
                }
                state.metrics.bytes = bytes;
                // 工作线程只解码描述；身份解析和依赖提交在管理器所属线程执行。
                asset_reference_context references{service};
                for (std::size_t slot = 0; slot < description->texture_uris.size(); ++slot) {
                  const auto& uri = description->texture_uris[slot];
                  if (uri.empty())
                    continue;
                  auto resolved = references.resolve(runtime_type_of<engine::texture>(), uri);
                  if (!resolved) {
                    diagnostic = std::move(resolved).error();
                    diagnostic.path = "/texture_uris/" + std::to_string(slot) + diagnostic.path;
                    return diagnostic.error;
                  }
                  work->texture_references[slot] =
                      resolved->alias(static_cast<const engine::texture*>(resolved->get()));
                }
                // 先提交全部唯一依赖，再观察完成；一个慢请求不阻止后续请求启动。
                for (const auto& source : references.dependencies()) {
                  auto dependency = service.load(source);
                  if (!dependency) {
                    work->dependency_error = dependency.error();
                    state.diagnostic.message += source.uri + ": 纹理请求失败\n";
                    continue;
                  }
                  work->dependencies.push_back(std::move(*dependency));
                }
                work->description = std::move(description);
                return error::success;
              }
            },
            work->cpu);
        if (status == error::not_ready)
          continue;
        if (status.failed()) {
          state.status = status;
          if (!diagnostic.message.empty())
            state.diagnostic = std::move(diagnostic);
          continue;
        }
        if (!work->description) {
          state.status = error::success;
          continue;
        }
      }
      // 请求保持观察权；取消外部观察者不影响材质自身等待。
      if (std::ranges::any_of(work->dependencies, [](const auto& dependency) {
            return dependency.status() == error::not_ready;
          }))
        continue;
      for (const auto& dependency : work->dependencies) {
        const auto status = dependency.status();
        if (status.failed()) {
          work->dependency_error = status;
          const auto failure = dependency.diagnostic();
          const auto uri = resource_uri(dependency.get<engine::texture>());
          state.diagnostic.path = failure.path.empty() ? std::string(uri) : failure.path;
          state.diagnostic.byte_offset = failure.byte_offset;
          state.diagnostic.message += std::string(uri) + ": " + failure.message + "\n";
        }
      }
      // 首次加载允许纹理占位；已有材质更新失败时保留旧内容。
      if (work->dependency_error.failed() &&
          render_.material_status(resource_rid(state.target)).ok()) {
        state.status = work->dependency_error;
        continue;
      }
      state.status =
          adopt(service, *work->description, resource_cast<engine::material>(state.target),
                std::move(work->texture_references));
      if (state.status.ok() && work->dependency_error.failed())
        state.status = work->dependency_error;
    } catch (const std::bad_alloc&) {
      state.status = error::out_of_memory;
    } catch (...) {
      state.status = error::internal;
    }
  }
  if (std::ranges::any_of(
          pending, [](const auto& work) { return work->observer->status != error::not_ready; }))
    service.notify_progress();
  std::erase_if(pending, [](const auto& work) {
    const bool finished =
        work->observer.use_count() == 1 || work->observer->status != error::not_ready;
    if (finished)
      if (auto entry = work->entry.lock(); entry && entry->loading == work)
        entry->loading.reset();
    return finished;
  });
}

} // namespace gneiss::engine::detail
