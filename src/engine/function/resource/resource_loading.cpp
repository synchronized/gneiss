// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/render/render_resource_data.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/resource/detail/resource_service_impl.hpp"
#include "engine/function/resource/resource_service.hpp"
#include <algorithm>
namespace gneiss::engine {
struct resource_service::implementation::pending_load {
  std::shared_ptr<detail::resource_load_state> state;
  std::variant<asset_data_request<mesh_data>, asset_data_request<texture_data>,
               asset_request<material_description>>
      cpu;
  asset_ref<const material_description> description;
  std::array<resource_request, 5> dependencies;
  std::size_t dependency_cursor{};
  std::size_t limit{};
};
result resource_service::load(const asset_source& source, resource_request& output, bool reload,
                              std::size_t limit) try {
  if (!impl_ || !impl_->loading_configured || render_stopping_)
    return result::invalid_state;
  if (limit == 0 || limit > maximum_prepared_bytes)
    return result::invalid_argument;
  render_asset_ref target;
  const auto reserved = reserve_asset(source, target);
  if (reserved.failed())
    return reserved;
  // 同一身份只允许一个实际工作；重载不制造并行版本或抢占已有请求。
  for (const auto& pending : impl_->pending) {
    if (pending->state->target == target && pending->state.use_count() > 1 &&
        pending->state->status == result::not_ready) {
      output.state_ = pending->state;
      return result::success;
    }
  }
  auto state = std::make_shared<detail::resource_load_state>();
  state->target = target;
  if (!reload && find(source, target, profile()).ok()) {
    state->status = result::success;
    output.state_ = std::move(state);
    return result::success;
  }
  auto work = std::make_shared<implementation::pending_load>();
  work->state = state;
  work->limit = limit;
  result status;
  switch (source.type) {
  case asset_type::mesh:
    status = assets_.load_mesh_data(source.uri, work->cpu.emplace<asset_data_request<mesh_data>>(),
                                    limit);
    break;
  case asset_type::texture:
    status = assets_.load_texture_data(
        source.uri, profile(), work->cpu.emplace<asset_data_request<texture_data>>(), limit);
    break;
  case asset_type::material:
    status =
        assets_.load(source.uri, work->cpu.emplace<asset_request<material_description>>(), reload);
    break;
  default:
    return result::invalid_argument;
  }
  if (status.failed())
    return status;
  impl_->pending.push_back(std::move(work));
  output.state_ = std::move(state);
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

void resource_service::implementation::stop_loading() noexcept {
  for (auto& work : pending)
    work->state->status = result::invalid_state;
  pending.clear();
}
void resource_service::implementation::advance(resource_service& service) {
  // 依赖请求可能追加到 pending；每次只处理开始时已有的工作。
  const auto count = pending.size();
  for (std::size_t i = 0; i < count; ++i) {
    auto work = pending[i];
    auto& state = *work->state;
    if (work->state.use_count() == 1 || state.status != result::not_ready)
      continue;
    try {
      if (!work->description) {
        meta::serialization_diagnostic diagnostic;
        const auto status = std::visit(
            [&](auto& request) -> result {
              using T = std::decay_t<decltype(request)>;
              if constexpr (std::is_same_v<T, asset_data_request<mesh_data>>) {
                mesh_data data;
                const auto status = request.take(data, &diagnostic, &state.metrics);
                if (status.failed())
                  return status;
                return adopt(service, std::move(data), asset_cast<mesh_resource>(state.target),
                             state.metrics.sources);
              } else if constexpr (std::is_same_v<T, asset_data_request<texture_data>>) {
                texture_data data;
                const auto status = request.take(data, &diagnostic, &state.metrics);
                if (status.failed())
                  return status;
                return adopt(service, std::move(data), asset_cast<texture_resource>(state.target),
                             state.metrics.sources);
              } else {
                asset_ref<const material_description> description;
                const auto status = request.take(description, &diagnostic);
                if (status.failed())
                  return status;
                auto bytes = sizeof(material_data);
                if (bytes > work->limit)
                  return result::out_of_memory;
                for (const auto& uri : description->texture_uris) {
                  if (uri.size() > work->limit - bytes)
                    return result::out_of_memory;
                  bytes += uri.size();
                }
                state.metrics.bytes = bytes;
                work->description = std::move(description);
                return result::success;
              }
            },
            work->cpu);
        if (status == result::not_ready)
          continue;
        if (status.failed()) {
          state.status = status;
          if (!diagnostic.message.empty())
            state.diagnostic = {status, diagnostic.byte_offset, std::move(diagnostic.path),
                                std::move(diagnostic.message)};
          continue;
        }
        if (!work->description) {
          state.status = result::success;
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
            const auto status = service.load<texture_resource>(uri, dependency);
            if (status.failed()) {
              state.status = status;
              break;
            }
          }
          const auto status = dependency.status();
          if (status == result::not_ready)
            break;
          if (status.failed()) {
            state.status = status;
            state.diagnostic = dependency.diagnostic();
            break;
          }
          ++work->dependency_cursor;
        }
        if (state.status.failed() && state.status != result::not_ready)
          continue;
        if (work->dependency_cursor < work->description->texture_uris.size())
          continue;
      }
      std::array<texture, 5> dependencies;
      for (std::size_t slot = 0; slot < dependencies.size(); ++slot)
        dependencies[slot] = work->dependencies[slot].get<texture_resource>();
      state.status = adopt(service, *work->description, asset_cast<material_resource>(state.target),
                           std::move(dependencies));
    } catch (const std::bad_alloc&) {
      state.status = result::out_of_memory;
    } catch (...) {
      state.status = result::internal;
    }
  }
  if (notification && std::ranges::any_of(pending, [](const auto& work) {
        return work->state->status != result::not_ready;
      }))
    notification->notify();
  std::erase_if(pending, [](const auto& work) {
    return work->state.use_count() == 1 || work->state->status != result::not_ready;
  });
}
}
