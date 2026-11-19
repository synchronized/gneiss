// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "detail/asset_construction.hpp"
#include "detail/dependency_graph.hpp"
#include "detail/load_operation.hpp"
#include "detail/render_load_operation.hpp"
#include "function/asset/detail/loader_factory.hpp"
#include "resource/material/detail/material_resource_access.hpp"
#include "resource/mesh/detail/mesh_resource_access.hpp"
#include "resource/scene/detail/scene_asset_access.hpp"
#include "resource/texture/detail/texture_resource_access.hpp"
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/meta/registry.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/core/base/type_id.hpp>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/render/service.hpp>
#include <gneiss/engine/function/files/service.hpp>
#include <gneiss/engine/resource/asset_uri.hpp>
#include <gneiss/engine/resource/scene/scene_validation.hpp>
#include <gneiss/engine/function/asset/loaders/texture_decoder.hpp>

#include "function/asset/resource_cache.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <stdexcept>

namespace {

using namespace gneiss;

gneiss::type_id cache_type(gneiss::render_resource_kind type) noexcept {
  using gneiss::render_resource_kind;
  switch (type) {
  case render_resource_kind::mesh:
    return gneiss::runtime_type_of<gneiss::mesh>();
  case render_resource_kind::material:
    return gneiss::runtime_type_of<gneiss::material>();
  case render_resource_kind::texture:
    return gneiss::runtime_type_of<gneiss::texture>();
  default:
    return gneiss::runtime_type_of<void>();
  }
}

} // namespace

namespace gneiss {
// 管理器唯一实现：拥有缓存、加载调度及构造适配器；接入宿主执行器，不创建私有线程池。
struct asset_manager::implementation {
  // 构造适配器最后析构，覆盖加载操作及缓存的成员析构期。
  std::unique_ptr<detail::asset_construction> construction;
  void advance(asset_manager& owner);
  void collect_unused();
  resource_cache cache;
  struct operation : detail::load_operation {
    cache_conversion conversion;
    std::shared_ptr<asset_loader> loader;
    gneiss::files::request_id read_request;
    gneiss::tasks::task_handle task;
    double queue_ms{};
    gneiss::files::bytes bytes;
    std::string pending_uri;
    std::size_t source_bytes{};
    bool reading{true}, read_complete{};
    std::shared_ptr<const void> value;
    serialization::diagnostic diagnostic;
    // 在途重载保活旧对象；失败后旧内容仍可复用。
    std::shared_ptr<const void> previous;
    std::atomic_bool settled{false};
    error status{error::not_ready};
    explicit operation(std::shared_ptr<asset_loader> parser) : loader(std::move(parser)) {}
    void decode() {
      const auto started = std::chrono::steady_clock::now();
      if (bytes.size() > std::numeric_limits<std::size_t>::max() - loader->input_bytes) {
        status = error::out_of_memory;
        return;
      }
      loader->input_bytes += bytes.size();
      if (!loader->metrics.sources.empty()) {
        loader->metrics.sources.back().bytes = bytes.size();
        loader->metrics.sources.back().digest = sha256(bytes);
      }
      loader->decode(bytes);
      source_bytes = loader->input_bytes;
      status = loader->status;
      value = loader->value;
      diagnostic = loader->diagnostic;
      if (status == error::not_ready || (status.ok() && !value && loader->next_uri.empty()))
        status = error::internal;
      loader->metrics.decode_ms +=
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
              .count();
      files::bytes{}.swap(bytes);
    }
  };
  std::map<detail::render_load_operation*, std::shared_ptr<detail::render_load_operation>>
      rendering;
  std::shared_ptr<detail::operation_queue<detail::render_load_operation>> render_ready =
      std::make_shared<detail::operation_queue<detail::render_load_operation>>();
  // 执行器丢弃排队任务时也产生终态；所有数据先写入，再 release 发布。
  struct settlement {
    std::shared_ptr<operation> work;
    bool ran{};
    ~settlement() {
      if (!ran) {
        work->status = error::invalid_state;
        work->diagnostic.message = "解码任务未执行";
      }
      work->settled.store(true, std::memory_order_release);
      work->notify();
    }
  };
  std::shared_ptr<gneiss::files::service> files;
  gneiss::tasks::task_executor* executor{};
  texture_prepare_profile profile{};
  gneiss::tasks::task_scope scope{};
  bool frozen{}, stopped{};
  std::map<type_id, std::shared_ptr<const asset_decoder>> decoders;
  std::map<type_id, loader_factory> loaders;
  std::map<type_id, reference_factory> references;
  std::map<operation*, std::shared_ptr<operation>> active;
  std::shared_ptr<detail::operation_queue<operation>> ready =
      std::make_shared<detail::operation_queue<operation>>();
  std::map<std::uint64_t, std::weak_ptr<operation>> tasks;
  // 仅容量不足的提交进入重试集合，不包含等待正常完成的操作。
  std::vector<std::weak_ptr<operation>> retry;

  error read(const std::shared_ptr<operation>& work, std::string_view uri) {
    if (work->loader->source_type) {
      std::shared_ptr<const files::source_domain> domain;
      auto status = files->resolve_source(uri, domain);
      if (status.failed())
        return status;
      work->loader->metrics.sources.push_back(
          {.identity = {std::move(domain), std::string(uri), work->loader->source_type}});
    }
    const auto submitted = files->read(
        uri,
        [weak = std::weak_ptr(work),
         path = std::string(uri)](files::completion<files::bytes> completed) {
          if (auto operation = weak.lock()) {
            operation->status = completed.status;
            if (completed.status.failed())
              operation->diagnostic = {0, path, "文件读取失败", completed.status};
            operation->bytes = std::move(completed.data);
            operation->read_complete = true;
            operation->notify();
          }
        },
        work->read_request);
    if (submitted.failed()) {
      if (work->loader->source_type)
        work->loader->metrics.sources.pop_back();
      return submitted;
    }
    work->loader->next_uri.clear();
    work->reading = true;
    work->read_complete = false;
    work->settled.store(false, std::memory_order_relaxed);
    return submitted;
  }
  bool schedule(const std::shared_ptr<operation>& work) {
    work->settled.store(false, std::memory_order_relaxed);
    auto completion = std::make_shared<settlement>();
    completion->work = work;
    const auto submitted = executor->submit(
        {.name = "asset-decode", .scope = scope},
        [completion](const auto& context) {
          auto& task = *completion->work;
          {
            if (context.stop_requested()) {
              task.status = error::invalid_state;
              task.diagnostic.message = "解码任务收到停止请求";
            } else {
              task.decode();
            }
          }
          completion->ran = true;
          return gneiss::tasks::task_outcome{};
        },
        work->task);
    if (submitted == gneiss::tasks::submit_result::full) {
      completion->ran = true;
      work->reading = true;
      retry.push_back(work);
      return false;
    }
    if (submitted != gneiss::tasks::submit_result::success)
      work->status = error::invalid_state;
    else
      tasks.emplace(work->task.id, work);
    return true;
  }
  ~implementation() { stop(); }
  void request_stop() {
    if (stopped)
      return;
    stopped = true;
    for (const auto& [key, work] : rendering) {
      (void)key;
      work->stop();
    }
    rendering.clear();
    if (executor)
      executor->cancel_scope(scope);
    for (const auto& [key, work] : active) {
      (void)key;
      if (work->reading && work->read_request.value != 0)
        (void)files->cancel(work->read_request);
      work->stop();
    }
  }
  bool finish_stop() noexcept {
    if (!stopped)
      return false;
    // 停止提交后 idle 不会重新变忙；只在全部 CPU 任务退出后回收 scope。
    if (executor) {
      if (!executor->idle(scope))
        return false;
      if (!executor->close_scope(scope))
        return false;
      executor = nullptr;
    }
    active.clear();
    tasks.clear();
    retry.clear();
    cache.clear_loading();
    files.reset();
    return true;
  }
  void stop() noexcept try {
    request_stop();
    // 迁移期析构兜底；宿主应先 request_stop 并推进到 finish_stop 完成。
    if (executor) {
      (void)executor->close_scope(scope);
      executor = nullptr;
    }
    (void)finish_stop();
    collect_unused();
    if (construction)
      construction->close();
  } catch (...) {
    gneiss::fatal_current_exception();
  }
};
std::shared_ptr<gneiss::files::service> asset_manager::async_files() const noexcept {
  return impl_->files;
}
result<asset_identity> asset_manager::identify(std::string_view uri, std::uint32_t type) const {
  if (!impl_->files || type == 0U)
    return error::invalid_state;
  {
    std::shared_ptr<const gneiss::files::source_domain> domain;
    const auto status = impl_->files->resolve_source(uri, domain);
    if (status.failed())
      return status;
    return asset_identity{.source = std::move(domain), .uri = std::string(uri), .type = type};
  }
}

asset_manager::asset_manager(std::shared_ptr<gneiss::files::service> files)
    : impl_(std::make_unique<implementation>()) {
  if (!files)
    throw std::invalid_argument("文件服务不能为空");
  impl_->files = std::move(files);
  const auto bind = [&]<class T>() {
    impl_->references.emplace(runtime_type_of<T>(), reference_factory_for<T>());
  };
  bind.template operator()<texture>();
  bind.template operator()<mesh>();
  bind.template operator()<material>();
  bind.template operator()<scene>();
  bind.template operator()<prefab>();
  bind.template operator()<gneiss::input::action_map>();
  auto frozen = meta::is_frozen();
  if (!frozen)
    throw std::invalid_argument("资源 Registry 必须冻结");
}
error asset_manager::store_erased(type_id type, std::string_view uri,
                                  std::shared_ptr<const asset> value,
                                  std::shared_ptr<const asset>& output,
                                  asset_load_options options) {
  auto& state = *impl_;
  if (state.stopped)
    return error::invalid_state;
  if (!value || validate_uri(uri).failed())
    return error::invalid_argument;
  std::shared_ptr<const gneiss::files::source_domain> domain;
  auto status = state.files->resolve_source(uri, domain);
  if (status.failed())
    return status;
  const auto parameters = asset_load_parameters(options);
  auto record = cache().acquire({domain, std::string(uri), type, parameters});
  auto stored = cache().load(
      uri, type, [&]() -> result<std::shared_ptr<const asset>> { return value; }, domain, true, 0,
      parameters);
  if (!stored)
    return stored.error();
  state.frozen = true;
  record->loading.reset();
  output = stored->resource;
  return error::success;
}
asset_manager::~asset_manager() = default;
detail::asset_construction* asset_manager::construction() noexcept {
  return impl_->construction.get();
}
resource_cache& asset_manager::cache() noexcept { return impl_->cache; }
const resource_cache& asset_manager::cache() const noexcept { return impl_->cache; }
std::size_t asset_manager::cached_count() const noexcept { return cache().size(); }
bool asset_manager::stop_requested() const noexcept { return impl_->stopped; }
bool asset_manager::loading_configured() const noexcept {
  return impl_->executor && !stop_requested();
}
error asset_manager::configure(gneiss::tasks::task_executor& executor) {
  return configure(executor, impl_->profile, impl_->scope);
}
error asset_manager::configure(gneiss::tasks::task_executor& executor,
                               texture_prepare_profile profile, gneiss::tasks::task_scope scope) {
  auto& state = *impl_;
  if (state.stopped)
    return error::invalid_state;
  if (state.executor) {
    return state.executor == &executor && state.profile == profile &&
                   (!scope.id || (state.scope.id == scope.id && state.scope.owner == scope.owner))
               ? error::success
               : error::invalid_state;
  }
  if (scope.id && !executor.idle(scope))
    return error::invalid_argument;
  if (!scope.id)
    scope = executor.make_scope();
  if (!scope.id)
    return error::invalid_state;
  state.scope = scope;
  state.executor = &executor;
  state.profile = profile;
  return error::success;
}
error asset_manager::attach_render(gneiss::render::service& render) {
  if (impl_->stopped)
    return error::invalid_state;
  if (impl_->construction)
    return &impl_->construction->render() == &render ? error::success : error::invalid_state;
  auto next = std::make_unique<detail::asset_construction>(render);
  impl_->construction = std::move(next);
  return error::success;
}
texture_prepare_profile asset_manager::profile() const noexcept { return impl_->profile; }
result<asset_request<asset>> asset_manager::load(type_id type, std::string_view uri,
                                                 asset_load_options options) {
  const auto found = impl_->references.find(type);
  if (found == impl_->references.end())
    return error::unsupported;
  return found->second(*this, uri, options);
}
error asset_manager::register_loader(type_id type, loader_factory factory,
                                     reference_factory reference) {
  auto& state = *impl_;
  if (state.stopped || state.frozen)
    return error::invalid_state;
  if (!factory || type == runtime_type_of<void>() || type == runtime_type_of<scene>() ||
      type == runtime_type_of<prefab>() || type == runtime_type_of<mesh>() ||
      type == runtime_type_of<texture>() || type == runtime_type_of<material>() ||
      type == runtime_type_of<gneiss::input::action_map>())
    return error::invalid_argument;
  if (state.loaders.contains(type) || !reference)
    return error::invalid_argument;
  state.references.emplace(type, reference);
  try {
    state.loaders.emplace(type, std::move(factory));
  } catch (...) {
    state.references.erase(type);
    throw;
  }
  return error::success;
}
error asset_manager::register_decoder(const std::shared_ptr<const asset_decoder>& loader) {
  auto& state = *impl_;
  if (state.stopped || state.frozen) {
    return error::invalid_state;
  }
  if (!loader) {
    return error::invalid_argument;
  }
  const auto type = loader->target_type();
  if (type == runtime_type_of<scene_description>() || type == runtime_type_of<prefab_description>())
    return error::unsupported;
  // 内置多阶段 Loader 不接受单缓冲区解码器覆盖。
  if (type == runtime_type_of<mesh_data>() || type == runtime_type_of<texture_data>() ||
      type == runtime_type_of<scene>() || type == runtime_type_of<prefab>() ||
      type == runtime_type_of<gneiss::input::action_map>())
    return error::invalid_argument;
  return state.decoders.emplace(type, loader).second ? error::success : error::invalid_argument;
}
error asset_manager::resolve_decoder(type_id type, decoder_factory fallback,
                                     std::shared_ptr<const asset_decoder>& output) {
  auto& state = *impl_;
  state.frozen = true;
  const auto found = state.decoders.find(type);
  if (found != state.decoders.end()) {
    output = found->second;
    return error::success;
  }
  auto decoded = fallback();
  if (!decoded)
    return decoded.error();
  output = std::move(*decoded);
  state.decoders.emplace(type, output);
  return error::success;
}

error asset_manager::request(type_id type, std::string_view uri, asset_load_options options,
                             std::shared_ptr<detail::asset_state>& output, decoder_factory fallback,
                             cache_conversion conversion) {
  auto& state = *impl_;
  if (state.stopped) {
    return error::invalid_state;
  }
  if (state.executor == nullptr) {
    return error::not_ready;
  }
  if (validate_uri(uri).failed()) {
    return error::invalid_argument;
  }
  std::shared_ptr<const gneiss::files::source_domain> domain;
  const auto identified = state.files->resolve_source(uri, domain);
  if (identified != error::success)
    return identified;
  const auto custom = state.loaders.find(type);
  result<std::shared_ptr<asset_loader>> task =
      custom != state.loaders.end()
          ? result<std::shared_ptr<asset_loader>>{custom->second({domain, std::string(uri), 0},
                                                                 options)}
          : detail::make_asset_loader(
                type, std::string(uri), options,
                [&]() -> result<std::shared_ptr<const asset_decoder>> {
                  std::shared_ptr<const asset_decoder> decoder;
                  auto status = type == runtime_type_of<gneiss::input::action_map>()
                                    ? resolve_decoder(
                                          runtime_type_of<gneiss::input::action_map_description>(),
                                          &detail::make_reflected_decoder<
                                              gneiss::input::action_map_description>,
                                          decoder)
                                    : resolve_decoder(type, fallback, decoder);
                  return status.failed() ? result<std::shared_ptr<const asset_decoder>>{status}
                                         : decoder;
                },
                domain);
  if (!task)
    return task.error();
  if (!*task)
    return error::internal;
  state.frozen = true;
  std::shared_ptr<detail::asset_entry> entry;
  std::optional<detail::cached_asset> cached;
  if (conversion.to_asset) {
    const auto parameters = asset_load_parameters(options);
    entry = cache().acquire({domain, std::string(uri), type, parameters});
    if (entry->loading && entry->loading->has_observers()) {
      output = entry->loading->observer;
      return error::success;
    }
    cached = cache().lookup(uri, type, domain, parameters);
    if (!options.reload && cached && cached->ready) {
      auto ready = std::make_shared<detail::asset_state>();
      ready->status = error::success;
      ready->source_bytes = cached->source_bytes;
      ready->metrics = cached->metrics;
      ready->value = conversion.to_value(cached->resource);
      ready->identity = cached->resource;
      ready->snapshot = conversion.snapshot(cached->resource);
      output = std::move(ready);
      return error::success;
    }
  }
  // 内部解码由父资产操作持有；不另建身份、弱缓存或在途索引。
  auto work = std::make_shared<implementation::operation>(std::move(*task));
  work->entry = entry;
  work->conversion = conversion;
  work->previous = cached ? conversion.to_value(cached->resource) : nullptr;
  if (conversion.to_asset) {
    work->observer->identity = cached ? cached->resource : work->loader->reserve();
    if (work->observer->identity && !work->previous)
      work->previous = conversion.to_value(work->observer->identity);
  }
  detail::connect_operation(work, state.ready);
  state.active.emplace(work.get(), work);
  try {
    const auto submitted = state.read(work, uri);
    if (submitted == error::not_ready) {
      work->pending_uri = uri;
      work->reading = false;
      state.retry.push_back(work);
    } else if (submitted.failed()) {
      state.active.erase(work.get());
      return submitted;
    }
  } catch (...) {
    state.active.erase(work.get());
    throw;
  }
  if (entry) {
    if (!cached) {
      entry->ready = false;
      entry->owned_dependencies.clear();
    }
    entry->pending_dependencies.clear();
    entry->loading = work;
    if (work->observer->identity)
      entry->cached = work->observer->identity;
  }
  output = work->observer;
  return error::success;
}
error asset_manager::find_erased(type_id type, std::string_view uri,
                                 std::shared_ptr<const asset>& output,
                                 asset_load_options options) const {
  if (impl_->stopped || !impl_->files)
    return error::invalid_state;
  if (validate_uri(uri).failed())
    return error::invalid_argument;
  std::shared_ptr<const gneiss::files::source_domain> source;
  const auto status = impl_->files->resolve_source(uri, source);
  if (status.failed())
    return status;
  const auto entry = cache().lookup(uri, type, std::move(source), asset_load_parameters(options));
  if (!entry || !entry->resource)
    return error::not_found;
  if (!entry->ready)
    return error::not_ready;
  output = entry->resource;
  return error::success;
}

asset_diagnostic asset_manager::advance_dependencies(
    const std::shared_ptr<detail::asset_entry>& owner,
    std::span<const std::shared_ptr<asset_dependency>> dependencies) {
  bool waiting = false;
  for (const auto& dependency : dependencies) {
    if (dependency->type == runtime_type_of<texture>() ||
        dependency->type == runtime_type_of<mesh>() ||
        dependency->type == runtime_type_of<material>())
      if (dependency->options.texture_profile == texture_prepare_profile{})
        dependency->options.texture_profile = profile();
    if (owner) {
      std::shared_ptr<const files::source_domain> domain;
      auto resolved = impl_->files->resolve_source(dependency->uri, domain);
      if (resolved.failed())
        return {resolved, 0, dependency->path, dependency->uri};
      auto target = cache().acquire(
          {domain, dependency->uri, dependency->type, asset_load_parameters(dependency->options)});
      auto linked = detail::link_dependency(owner, target, dependency->path);
      if (linked.error.failed())
        return linked;
    }
    const auto status = dependency->advance(*this);
    if (status == error::not_ready)
      waiting = true;
    else if (status.failed())
      return dependency->diagnostic;
  }
  return {waiting ? error::not_ready : error::success, 0, {}, {}};
}
void asset_manager::advance() { impl_->advance(*this); }
void asset_manager::collect_unused() { impl_->collect_unused(); }
void asset_manager::implementation::advance(asset_manager& owner) {
  auto& state = *this;
  if (construction) {
    (void)construction->drain();
    detail::advance_load_operations(
        state.rendering, *state.render_ready, [&](const auto& work) { work->advance(owner); },
        [](const auto&) {});
  }
  if (state.stopped) {
    (void)state.finish_stop();
    return;
  }
  if (state.executor == nullptr) {
    return;
  }
  // 文件完成由宿主统一分发；这里只处理 CPU 解码与资源结果。
  std::vector<gneiss::tasks::task_completion> completions;
  (void)state.executor->poll(state.scope, completions);
  for (const auto& completion : completions) {
    auto found = state.tasks.find(completion.task.id);
    if (found == state.tasks.end())
      continue;
    if (auto work = found->second.lock()) {
      work->queue_ms += completion.queue_ms;
      work->task = {};
      work->notify();
    }
    state.tasks.erase(found);
  }
  auto retries = std::exchange(state.retry, {});
  for (const auto& weak : retries)
    if (auto work = weak.lock())
      work->notify();
  detail::advance_load_operations(
      state.active, *state.ready,
      [&](const auto& work) {
        if (!work->pending_uri.empty()) {
          {
            work->status = state.read(work, work->pending_uri);
          }
          if (work->status == error::not_ready) {
            state.retry.push_back(work);
            return;
          }
          work->pending_uri.clear();
          if (work->status.failed())
            work->settled.store(true, std::memory_order_release);
        }
        if (work->reading) {
          if (!work->read_complete) {
            return;
          }
          work->reading = false;
          if (work->status == error::success) {
            {
              if (!state.schedule(work)) {
                return;
              }
            }
          } else {
            work->settled.store(true, std::memory_order_release);
          }
        }
        if (!work->settled.load(std::memory_order_acquire)) {
          return;
        }
        if (work->conversion.to_asset && !work->is_current()) {
          work->complete(error::invalid_state, {.message = "CPU 操作身份已替换"});
          return;
        }
        if (work->status.ok() && !work->loader->next_uri.empty()) {
          {
            const auto next_uri = work->loader->next_uri;
            work->status = state.read(work, next_uri);
            if (work->status == error::not_ready) {
              work->pending_uri = next_uri;
              state.retry.push_back(work);
              return;
            }
          }
          if (work->status.ok()) {
            return;
          }
        }
        if (work->status.ok()) {
          {
            for (const auto& dependency : work->loader->dependencies)
              dependency->notify = work->notify;
            auto dependencies =
                owner.advance_dependencies(work->entry.lock(), work->loader->dependencies);
            if (dependencies.error == error::not_ready)
              return;
            if (dependencies.error.failed()) {
              work->status = dependencies.error;
              work->loader->reject(std::move(dependencies));
            }
            if (work->status.ok())
              work->status = work->loader->finish();
            work->loader->dependencies.clear();
            work->value = work->loader->value;
            work->diagnostic = work->loader->diagnostic;
          }
        }
        work->loader->metrics.queue_ms = work->queue_ms;
        if (work->is_current()) {
          auto entry = work->entry.lock();
          if (work->status == error::success) {
            work->loader->adopt(work->previous);
            work->value = work->loader->value;
            const auto& [domain, uri, type, parameters] = entry->identity;
            auto cached = cache.load(
                uri, type,
                [&]() -> result<std::shared_ptr<const asset>> {
                  return work->conversion.to_asset(work->value);
                },
                domain, true, work->source_bytes, parameters, work->loader->metrics);
            work->status = cached ? error::success : cached.error();
            if (cached) {
              work->value = work->conversion.to_value(cached->resource);
              work->observer->snapshot = work->conversion.snapshot(cached->resource);
            }
          }
        }
        work->complete(work->status, std::move(work->diagnostic), std::move(work->value),
                       work->source_bytes, std::move(work->loader->metrics));
      },
      [&](const auto& work) {
        if (work->task.id)
          (void)state.executor->cancel(work->task);
        if (work->reading)
          (void)state.files->cancel(work->read_request);
      });
}
void asset_manager::implementation::collect_unused() {
  // 资源强引用的最后释放不属于请求完成通知；回收入口只检查拥有权，不推进加载。
  for (auto iterator = rendering.begin(); iterator != rendering.end();) {
    auto work = iterator->second;
    if (work->has_observers()) {
      ++iterator;
      continue;
    }
    work->stop();
    iterator = rendering.erase(iterator);
  }
  do {
    cache.release_unused();
  } while (construction && construction->drain() != 0U);
}
void asset_manager::request_stop() { impl_->request_stop(); }
bool asset_manager::stopped() const noexcept {
  return impl_->stopped && !impl_->executor && !impl_->files;
}
void asset_manager::stop() noexcept { impl_->stop(); }

result<asset_request<render_resource_ref>> asset_manager::load(const render_resource_source& source,
                                                               asset_load_options options) {
  const bool reload = options.reload;
  if (options.texture_profile != texture_prepare_profile{} && options.texture_profile != profile())
    return error::invalid_argument;
  asset_request<render_resource_ref> output;
  if (!impl_->construction || !loading_configured())
    return error::invalid_state;
  auto reserved = acquire_resource(source);
  if (!reserved)
    return reserved.error();
  auto target = std::move(*reserved);
  auto identity = identify(source.uri, static_cast<std::uint32_t>(source.type));
  if (!identity)
    return identity.error();
  auto entry = cache().acquire({identity->source, source.uri, cache_type(source.type),
                                asset_load_parameters({.texture_profile = profile()})});
  if (entry->loading && entry->loading->has_observers() &&
      entry->loading->observer->status == error::not_ready) {
    output.state_ = entry->loading->observer;
    return output;
  }
  auto state = std::make_shared<detail::asset_state>();
  state->target = target;
  if (!reload && find(source, profile()).ok()) {
    state->status = error::success;
    if (auto resource = target.as<texture>())
      state->snapshot = resource->snapshot().shared();
    if (auto resource = target.as<mesh>())
      state->snapshot = resource->snapshot().shared();
    if (auto resource = target.as<material>())
      state->snapshot = resource->snapshot().shared();
    output.state_ = std::move(state);
    return output;
  }
  auto work = std::make_shared<detail::render_load_operation>();
  work->observer = state;
  work->entry = entry;
  work->source = source;
  work->reload = reload;
  detail::connect_operation(work, impl_->render_ready);
  impl_->rendering.emplace(work.get(), work);
  work->notify();
  entry->pending_dependencies.clear();
  entry->loading = work;
  output.state_ = std::move(state);
  return output;
}

result<ref<const gneiss::mesh>> asset_manager::create(const mesh_view& data) {
  if (!impl_->construction || stop_requested())
    return error::invalid_state;
  return impl_->construction->create_mesh(data);
}
result<ref<const gneiss::texture>> asset_manager::create(const texture_view& data) {
  if (!impl_->construction || stop_requested())
    return error::invalid_state;
  return impl_->construction->create_texture(data);
}
result<ref<const gneiss::material>>
asset_manager::create(const material_parameters& data,
                      std::array<ref<const gneiss::texture>, 5> deps) {
  if (!impl_->construction || stop_requested())
    return error::invalid_state;
  return impl_->construction->create_material(data, std::move(deps));
}

result<ref<const scene>> asset_manager::create(scene_content content) {
  if (stop_requested())
    return error::invalid_state;
  if (!content.description)
    return error::invalid_argument;
  auto valid = validate_scene_description(*content.description);
  if (valid.error.failed())
    return valid.error;
  for (const auto& object : content.description->objects) {
    if (!object.prefab)
      continue;
    auto found = content.prefabs.find(object.prefab->uri);
    if (found == content.prefabs.end() || !found->second.description || !found->second.resource ||
        found->second.resource->source().uri != object.prefab->uri)
      return error::invalid_argument;
  }
  return ref<const scene>{detail::scene_asset_access::make<scene>(
      {}, ref<const scene_content>{make_ref<scene_content>(std::move(content))})};
}
result<ref<const scene>> asset_manager::create(scene_description content) {
  return create(scene_content{
      ref<const scene_description>{make_ref<scene_description>(std::move(content))}, {}});
}
result<ref<const prefab>> asset_manager::create(prefab_description content) {
  if (stop_requested())
    return error::invalid_state;
  auto valid = validate_prefab_description(content);
  if (valid.error.failed())
    return valid.error;
  return ref<const prefab>{detail::scene_asset_access::make<prefab>(
      {}, ref<const prefab_description>{make_ref<prefab_description>(std::move(content))})};
}

bool asset_manager::owns(const render_resource_ref& value) const {
  if (auto asset = value.as<gneiss::mesh>())
    return owns(asset);
  if (auto asset = value.as<gneiss::texture>())
    return owns(asset);
  if (auto asset = value.as<gneiss::material>())
    return owns(asset);
  return false;
}

namespace {
render_resource_ref cached_resource(const detail::cached_asset& entry) noexcept {
  if (auto asset = entry.as<gneiss::mesh>())
    return asset;
  if (auto asset = entry.as<gneiss::texture>())
    return asset;
  if (auto asset = entry.as<gneiss::material>())
    return asset;
  return {};
}
}

result<render_resource_ref> asset_manager::find(const render_resource_source& source,
                                                texture_prepare_profile profile) const {
  if (!impl_->construction || stop_requested())
    return error::invalid_state;
  {
    auto identity = identify(source.uri, static_cast<std::uint32_t>(source.type));
    if (!identity)
      return identity.error();
    auto domain = std::move(identity->source);
    const auto current =
        cache().lookup(source.uri, cache_type(source.type), domain,
                       asset_load_parameters({.texture_profile = this->profile()}));
    if (!current) {
      return gneiss::error::not_found;
    }
    if (current->type != cache_type(source.type)) {
      return gneiss::error::invalid_argument;
    }
    render_resource_ref target;
    target = cached_resource(*current);
    if (!owns(target)) {
      return gneiss::error::invalid_handle;
    }
    const auto matches_profile = [&](const gneiss::texture& value) {
      auto content = value.snapshot();
      return content &&
             (profile.generation == 0U || content->manifest.empty() || content->profile == profile);
    };
    if (auto texture = target.as<gneiss::texture>(); texture && !matches_profile(*texture))
      return texture->snapshot() ? error::not_found : error::not_ready;
    if (auto material = target.as<gneiss::material>())
      for (const auto& texture : material->textures())
        if (texture && texture->snapshot() && !matches_profile(*texture))
          return error::not_found;
    bool ready = false;
    if (auto value = target.as<gneiss::texture>())
      ready = bool(value->snapshot());
    if (auto value = target.as<gneiss::mesh>())
      ready = bool(value->snapshot());
    if (auto value = target.as<gneiss::material>())
      ready = bool(value->snapshot());
    if (!ready)
      return error::not_ready;
    return cached_resource(*current);
  }
}

result<render_resource_ref> asset_manager::acquire_resource(const render_resource_source& source) {
  if (!impl_->construction || stop_requested())
    return error::invalid_state;
  {
    if (gneiss::validate_uri(source.uri) != gneiss::error::success) {
      return gneiss::error::invalid_argument;
    }
    auto identified = identify(source.uri, static_cast<std::uint32_t>(source.type));
    if (!identified)
      return identified.error();
    auto identity = std::move(*identified);
    auto cached = cache().load(
        source.uri, cache_type(source.type),
        [&]() -> result<std::shared_ptr<const asset>> {
          switch (source.type) {
          case render_resource_kind::mesh: {
            auto value = impl_->construction->reserve_mesh();
            if (!value)
              return value.error();
            detail::mesh_resource_access::set_source(**value, identity);
            return std::shared_ptr<const asset>(value->shared());
          }
          case render_resource_kind::texture: {
            auto value = impl_->construction->reserve_texture();
            if (!value)
              return value.error();
            detail::texture_resource_access::set_source(**value, identity);
            return std::shared_ptr<const asset>(value->shared());
          }
          case render_resource_kind::material: {
            auto value = impl_->construction->reserve_material();
            if (!value)
              return value.error();
            detail::material_resource_access::set_source(**value, identity);
            return std::shared_ptr<const asset>(value->shared());
          }
          default:
            return error::invalid_argument;
          }
        },
        identity.source, false, 0, asset_load_parameters({.texture_profile = profile()}));
    if (!cached)
      return cached.error();
    auto resource = cached_resource(*cached);
    if (!owns(resource))
      return error::invalid_handle;
    return resource;
  }
}

bool asset_manager::owns(const ref<const gneiss::mesh>& value) const {
  if (!impl_->construction)
    return false;
  if (!value ||
      impl_->construction->render().mesh_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::mesh));
  return identity && *identity == value->source();
}

bool asset_manager::owns(const ref<const gneiss::texture>& value) const {
  if (!impl_->construction)
    return false;
  if (!value ||
      impl_->construction->render().texture_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::texture));
  return identity && *identity == value->source();
}

bool asset_manager::owns(const ref<const gneiss::material>& value) const {
  if (!impl_->construction)
    return false;
  if (!value || impl_->construction->render().material_status(resource_handle(value)) ==
                    error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::material));
  return identity && *identity == value->source();
}
}
