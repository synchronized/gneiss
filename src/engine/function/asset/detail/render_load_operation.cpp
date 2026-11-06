// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "render_load_operation.hpp"
#include "asset_construction.hpp"
#include "engine/function/asset/asset_reference_context.hpp"
#include "engine/function/render/render_service.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace gneiss::engine::detail {
void render_load_operation::advance(asset_manager& service, asset_construction& construction) {
  auto& state = *observer;
  if (!has_observers() || state.status != error::not_ready)
    return;
  if (!is_current()) {
    stop();
    return;
  }
  const auto finish = [&](error status) {
    complete(status, std::move(state.diagnostic), {}, state.source_bytes, std::move(state.metrics));
  };
  try {
    if (!description) {
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
              return asset_construction::adopt(service, loaded->shared(),
                                               resource_cast<engine::mesh>(state.target),
                                               state.metrics.sources);
            } else if constexpr (std::is_same_v<T, asset_request<texture_data>>) {
              state.metrics = request.metrics();
              return asset_construction::adopt(service, loaded->shared(),
                                               resource_cast<engine::texture>(state.target),
                                               state.metrics.sources);
            } else {
              auto decoded_description = std::move(*loaded);
              auto bytes = sizeof(material_data);
              for (const auto& uri : decoded_description->texture_uris) {
                if (uri.size() > std::numeric_limits<std::size_t>::max() - bytes)
                  return error::out_of_memory;
                bytes += uri.size();
              }
              state.metrics.bytes = bytes;
              // 工作线程只解码描述；身份解析和依赖提交在管理器所属线程执行。
              asset_reference_context references{service};
              for (std::size_t slot = 0; slot < decoded_description->texture_uris.size(); ++slot) {
                const auto& uri = decoded_description->texture_uris[slot];
                if (uri.empty())
                  continue;
                auto resolved = references.resolve(runtime_type_of<engine::texture>(), uri);
                if (!resolved) {
                  diagnostic = std::move(resolved).error();
                  diagnostic.path = "/texture_uris/" + std::to_string(slot) + diagnostic.path;
                  return diagnostic.error;
                }
                texture_references[slot] =
                    resolved->alias(static_cast<const engine::texture*>(resolved->get()));
              }
              // 先提交全部唯一依赖，再观察完成；一个慢请求不阻止后续请求启动。
              for (const auto& source : references.dependencies()) {
                auto dependency = service.load(source);
                if (!dependency) {
                  dependency_error = dependency.error();
                  state.diagnostic.message += source.uri + ": 纹理请求失败\n";
                  continue;
                }
                dependencies.push_back(std::move(*dependency));
              }
              description = std::move(decoded_description);
              return error::success;
            }
          },
          cpu);
      if (status == error::not_ready)
        return;
      if (status.failed()) {
        if (!diagnostic.message.empty())
          state.diagnostic = std::move(diagnostic);
        finish(status);
        return;
      }
      if (!description) {
        finish(error::success);
        return;
      }
    }
    // 请求保持观察权；取消外部观察者不影响材质自身等待。
    if (std::ranges::any_of(dependencies, [](const auto& dependency) {
          return dependency.status() == error::not_ready;
        }))
      return;
    for (const auto& dependency : dependencies) {
      const auto status = dependency.status();
      if (status.failed()) {
        dependency_error = status;
        const auto failure = dependency.diagnostic();
        const auto uri = resource_uri(dependency.get<engine::texture>());
        state.diagnostic.path = failure.path.empty() ? std::string(uri) : failure.path;
        state.diagnostic.byte_offset = failure.byte_offset;
        state.diagnostic.message += std::string(uri) + ": " + failure.message + "\n";
      }
    }
    // 首次加载允许纹理占位；已有材质更新失败时保留旧内容。
    if (dependency_error.failed() &&
        construction.render().material_status(resource_rid(state.target)).ok()) {
      finish(dependency_error);
      return;
    }
    auto status = asset_construction::adopt(service, *description,
                                            resource_cast<engine::material>(state.target),
                                            std::move(texture_references));
    if (status.ok() && dependency_error.failed())
      status = dependency_error;
    finish(status);
  } catch (const std::bad_alloc&) {
    finish(error::out_of_memory);
  } catch (...) {
    finish(error::internal);
  }
}
} // namespace gneiss::engine::detail
