// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
#include "engine/function/resource/resource_service.hpp"
#include <algorithm>
#include <limits>
namespace gneiss::engine {
struct resource_service::implementation::pending_load {
  std::shared_ptr<detail::resource_load_state> state;
  std::variant<asset_request<mesh_data>, asset_request<texture_data>,
               asset_request<material_description>>
      cpu;
  asset_ref<const material_description> description;
  std::array<resource_request, 5> dependencies;
  std::size_t dependency_cursor{};
};
result<resource_request> resource_service::load(const render_resource_source& source,
                                                bool reload) try {
  resource_request output;
  if (!impl_ || !impl_->loading_configured || render_stopping_)
    return error::invalid_state;
  auto reserved = reserve_resource(source);
  if (!reserved)
    return reserved.error();
  auto target = std::move(*reserved);
  // 同一身份只允许一个实际工作；重载不制造并行版本或抢占已有请求。
  for (const auto& pending : impl_->pending) {
    if (pending->state->target == target && pending->state.use_count() > 1 &&
        pending->state->status == error::not_ready) {
      output.state_ = pending->state;
      return output;
    }
  }
  auto state = std::make_shared<detail::resource_load_state>();
  state->target = target;
  if (!reload && find(source, profile()).ok()) {
    state->status = error::success;
    output.state_ = std::move(state);
    return output;
  }
  auto work = std::make_shared<implementation::pending_load>();
  work->state = state;
  switch (source.type) {
  case render_resource_kind::mesh: {
    auto loaded = assets_.load<mesh_data>(source.uri, {.reload = reload});
    if (!loaded)
      return loaded.error();
    work->cpu = std::move(*loaded);
    break;
  }
  case render_resource_kind::texture: {
    auto loaded =
        assets_.load<texture_data>(source.uri, {.reload = reload, .texture_profile = profile()});
    if (!loaded)
      return loaded.error();
    work->cpu = std::move(*loaded);
    break;
  }
  case render_resource_kind::material: {
    auto loaded = assets_.load<material_description>(source.uri, reload);
    if (!loaded)
      return loaded.error();
    work->cpu = std::move(*loaded);
    break;
  }
  default:
    return error::invalid_argument;
  }
  impl_->pending.push_back(std::move(work));
  output.state_ = std::move(state);
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

void resource_service::implementation::stop_loading() noexcept {
  for (auto& work : pending)
    work->state->status = error::invalid_state;
  pending.clear();
}
void resource_service::implementation::advance(resource_service& service) {
  // 依赖请求可能追加到 pending；每次只处理开始时已有的工作。
  const auto count = pending.size();
  for (std::size_t i = 0; i < count; ++i) {
    auto work = pending[i];
    auto& state = *work->state;
    if (work->state.use_count() == 1 || state.status != error::not_ready)
      continue;
    try {
      if (!work->description) {
        meta::serialization_diagnostic diagnostic;
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
              if constexpr (std::is_same_v<T, asset_request<mesh_data>>) {
                state.metrics = request.metrics();
                return adopt(service, loaded->shared(), resource_cast<mesh_resource>(state.target),
                             state.metrics.sources);
              } else if constexpr (std::is_same_v<T, asset_request<texture_data>>) {
                state.metrics = request.metrics();
                return adopt(service, loaded->shared(),
                             resource_cast<texture_resource>(state.target), state.metrics.sources);
              } else {
                auto description = std::move(*loaded);
                auto bytes = sizeof(material_data);
                for (const auto& uri : description->texture_uris) {
                  if (uri.size() > std::numeric_limits<std::size_t>::max() - bytes)
                    return error::out_of_memory;
                  bytes += uri.size();
                }
                state.metrics.bytes = bytes;
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
            state.diagnostic = {status, diagnostic.byte_offset, std::move(diagnostic.path),
                                std::move(diagnostic.message)};
          continue;
        }
        if (!work->description) {
          state.status = error::success;
          continue;
        }
      }
      {
        // 材质拥有依赖观察权；取消外部纹理请求不会取消仍被材质等待的工作。
        while (work->dependency_cursor < work->description->texture_uris.size()) {
          const auto& uri = work->description->texture_uris[work->dependency_cursor];
          if (uri.empty()) {
            ++work->dependency_cursor;
            continue;
          }
          auto& dependency = work->dependencies[work->dependency_cursor];
          if (!dependency) {
            auto loaded = service.load<texture_resource>(uri);
            if (!loaded) {
              state.status = loaded.error();
              break;
            }
            dependency = std::move(*loaded);
          }
          const auto status = dependency.status();
          if (status == error::not_ready)
            break;
          if (status.failed()) {
            state.status = status;
            state.diagnostic = dependency.diagnostic();
            break;
          }
          ++work->dependency_cursor;
        }
        if (state.status.failed() && state.status != error::not_ready)
          continue;
        if (work->dependency_cursor < work->description->texture_uris.size())
          continue;
      }
      std::array<texture, 5> dependencies;
      for (std::size_t slot = 0; slot < dependencies.size(); ++slot)
        dependencies[slot] = work->dependencies[slot].get<texture_resource>();
      state.status = adopt(service, *work->description,
                           resource_cast<material_resource>(state.target), std::move(dependencies));
    } catch (const std::bad_alloc&) {
      state.status = error::out_of_memory;
    } catch (...) {
      state.status = error::internal;
    }
  }
  if (notification && std::ranges::any_of(pending, [](const auto& work) {
        return work->state->status != error::not_ready;
      }))
    notification->notify();
  std::erase_if(pending, [](const auto& work) {
    return work->state.use_count() == 1 || work->state->status != error::not_ready;
  });
}
}
