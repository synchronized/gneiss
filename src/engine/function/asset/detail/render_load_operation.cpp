// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render_load_operation.hpp"
#include "asset_construction.hpp"
#include "engine/function/render/service.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace gneiss::detail {
void render_load_operation::advance(asset_manager& service, asset_construction&) {
  auto& state = *observer;
  if (!has_observers() || state.status != error::not_ready)
    return;
  if (!is_current()) {
    state.diagnostic = {0, source.uri, "加载身份已被替换", error::invalid_state};
    complete(error::invalid_state, std::move(state.diagnostic));
    return;
  }
  const auto finish = [&](error status) {
    if (status.ok()) {
      if (auto resource = state.target.as<texture>())
        state.snapshot = resource->snapshot().shared();
      if (auto resource = state.target.as<mesh>())
        state.snapshot = resource->snapshot().shared();
      if (auto resource = state.target.as<material>())
        state.snapshot = resource->snapshot().shared();
    }
    if (status.failed() && state.diagnostic.path.empty())
      state.diagnostic.path = source.uri;
    complete(status, std::move(state.diagnostic), {}, state.source_bytes, std::move(state.metrics));
  };
  {
    if (!started) {
      auto begin = [&]<class T>() -> error {
        auto request = service.decode_data<T>(
            source.uri, {.reload = reload, .texture_profile = service.profile()});
        if (!request)
          return request.error();
        asset_request_access::when_ready(*request, notify);
        cpu = std::move(*request);
        return error::success;
      };
      error status;
      switch (source.type) {
      case render_resource_kind::mesh:
        status = begin.template operator()<mesh_data>();
        break;
      case render_resource_kind::texture:
        status = begin.template operator()<texture_data>();
        break;
      case render_resource_kind::material:
        status = begin.template operator()<material_description>();
        break;
      default:
        status = error::invalid_argument;
        break;
      }
      if (status == error::not_ready)
        return;
      if (status.failed()) {
        state.diagnostic.message = "无法提交 CPU 解码";
        finish(status);
        return;
      }
      started = true;
    }
    if (!description && !pending_mesh && !pending_texture) {
      serialization::diagnostic diagnostic;
      const auto status = std::visit(
          [&](auto& request) -> error {
            using T = std::decay_t<decltype(request)>;
            auto loaded = request.take();
            if (!loaded) {
              auto failure = std::move(loaded).error();
              if (failure.message.empty())
                failure.message = request ? "CPU 请求终态失败" : "CPU 请求已被领取";
              diagnostic = {failure.byte_offset, std::move(failure.path),
                            std::move(failure.message), failure.error};
              return failure.error;
            }
            state.source_bytes = request.source_bytes();
            if constexpr (std::is_same_v<T, asset_request<mesh_data>>) {
              state.metrics = request.metrics();
              pending_mesh = loaded->shared();
              return error::success;
            } else if constexpr (std::is_same_v<T, asset_request<texture_data>>) {
              state.metrics = request.metrics();
              pending_texture = loaded->shared();
              return error::success;
            } else {
              auto decoded_description = std::move(*loaded);
              auto bytes = sizeof(material_data);
              for (const auto& uri : decoded_description->texture_uris) {
                if (uri.size() > std::numeric_limits<std::size_t>::max() - bytes)
                  return error::out_of_memory;
                bytes += uri.size();
              }
              state.metrics.bytes = bytes;
              for (std::size_t slot = 0; slot < decoded_description->texture_uris.size(); ++slot) {
                const auto& uri = decoded_description->texture_uris[slot];
                if (uri.empty())
                  continue;
                auto dependency = std::make_shared<reference_dependency<gneiss::texture>>(
                    uri, asset_load_options{.texture_profile = service.profile()},
                    "/texture_uris/" + std::to_string(slot));
                texture_dependencies[slot] = dependency;
                dependencies.push_back(std::move(dependency));
              }
              description = std::move(decoded_description);
              return error::success;
            }
          },
          cpu);
      if (status == error::not_ready)
        return;
      if (status.failed()) {
        if (diagnostic.message.empty())
          diagnostic.message = "CPU 内容解码或采用失败";
        state.diagnostic = std::move(diagnostic);
        finish(status);
        return;
      }
    }
    if (pending_mesh || pending_texture) {
      const auto status =
          pending_mesh ? asset_construction::adopt(service, pending_mesh,
                                                   resource_cast<gneiss::mesh>(state.target),
                                                   state.metrics.sources)
                       : asset_construction::adopt(service, pending_texture,
                                                   resource_cast<gneiss::texture>(state.target),
                                                   state.metrics.sources);
      if (status == error::not_ready) {
        // 队列背压时保留已解码内容，仅在下一次管理器 update 重试采用。
        notify();
        return;
      }
      pending_mesh.reset();
      pending_texture.reset();
      finish(status);
      return;
    }
    for (const auto& dependency : dependencies)
      dependency->notify = notify;
    auto dependency_status = service.advance_dependencies(entry.lock(), dependencies);
    if (dependency_status.error == error::not_ready)
      return;
    if (dependency_status.error.failed()) {
      state.diagnostic = {dependency_status.byte_offset, std::move(dependency_status.path),
                          std::move(dependency_status.message), dependency_status.error};
      finish(dependency_status.error);
      return;
    }
    for (std::size_t slot = 0; slot < texture_dependencies.size(); ++slot)
      if (texture_dependencies[slot])
        texture_references[slot] = texture_dependencies[slot]->resource();
    auto status = asset_construction::adopt(service, *description,
                                            resource_cast<gneiss::material>(state.target),
                                            std::move(texture_references));
    if (status == error::not_ready) {
      notify();
      return;
    }
    finish(status);
  }
}
} // namespace gneiss::detail
