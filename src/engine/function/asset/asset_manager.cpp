// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "asset_manager.hpp"
#include "detail/load_operation.hpp"
#include "detail/asset_construction.hpp"
#include "detail/scene_loading.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/render/render_service.hpp"
#include "engine/function/asset/detail/loader_factory.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/material/detail/material_resource_access.hpp"
#include "engine/resource/mesh/detail/mesh_resource_access.hpp"
#include "engine/resource/texture/detail/texture_resource_access.hpp"
#include "engine/resource/texture/texture_decoder.hpp"

#include "resource_cache.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <stdexcept>

namespace {

using namespace gneiss::engine;

gneiss::engine::type_id cache_type(gneiss::engine::render_resource_kind type) noexcept {
  using gneiss::engine::render_resource_kind;
  switch (type) {
  case render_resource_kind::mesh:
    return gneiss::engine::runtime_type_of<gneiss::engine::mesh>();
  case render_resource_kind::material:
    return gneiss::engine::runtime_type_of<gneiss::engine::material>();
  case render_resource_kind::texture:
    return gneiss::engine::runtime_type_of<gneiss::engine::texture>();
  default:
    return gneiss::engine::runtime_type_of<void>();
  }
}

} // namespace

namespace gneiss::engine {
// 移植示例的类型/位置请求表、合并与只读发布；接入已有执行器，不创建私有线程池。
struct asset_manager::resource_state {
  resource_cache cache;
  using key = detail::asset_key;
  struct operation : detail::load_operation {
    std::weak_ptr<detail::asset_entry> entry;
    bool cache_result{true};
    std::shared_ptr<asset_loader> loader;
    engine::files::request_id read_request;
    task_handle task;
    double queue_ms{};
    engine::files::bytes bytes;
    std::size_t source_bytes{};
    bool reading{true}, read_complete{};
    std::shared_ptr<const void> value;
    serialization::diagnostic diagnostic;
    // 在途重载保活旧缓存条目；失败后旧内容仍可复用。
    std::shared_ptr<const detail::asset_cache_value> previous;
    std::atomic_bool settled{false};
    error status{error::not_ready};
    explicit operation(std::shared_ptr<asset_loader> parser)
        : loader(std::move(parser)) {}
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
  struct scene_work {
    std::unique_ptr<detail::scene_description_request> descriptions;
    scene_preparation_inputs inputs;
    std::shared_ptr<scene_instantiation_data> prepared{
        std::make_shared<scene_instantiation_data>()};
    scene_diagnostic diagnostic;
    std::string prefab_uri;
    std::shared_ptr<detail::asset_state> observer{std::make_shared<detail::asset_state>()};
    task_handle task;
    error status{error::invalid_state};
    bool completed{};
  };
  std::vector<std::shared_ptr<scene_work>> scenes;
  // 执行器丢弃排队任务时也产生终态；所有数据先写入，再 release 发布。
  struct settlement {
    std::shared_ptr<operation> work;
    bool ran{};
    ~settlement() {
      if (!ran) {
        work->status = error::invalid_state;
      }
      work->settled.store(true, std::memory_order_release);
    }
  };
  std::shared_ptr<engine::files::service> files;
  task_executor* executor{};
  texture_prepare_profile profile{};
  std::shared_ptr<progress_notification> notification;
  task_scope scope{};
  bool frozen{}, stopped{};
  std::map<type_id, std::shared_ptr<const asset_decoder>> decoders;
  std::vector<std::shared_ptr<operation>> active;

  error read(const std::shared_ptr<operation>& work, std::string_view uri) {
    if (work->loader->source_type) {
      std::shared_ptr<const files::source_domain> domain;
      auto status = files->resolve_source(uri, domain);
      if (status.failed())
        return status;
      work->loader->metrics.sources.push_back(
          {.identity = {std::move(domain), std::string(uri), work->loader->source_type}});
    }
    work->loader->next_uri.clear();
    work->reading = true;
    work->read_complete = false;
    work->settled.store(false, std::memory_order_relaxed);
    return files->read(
        uri,
        [weak = std::weak_ptr(work),
         path = std::string(uri)](files::completion<files::bytes> completed) {
          if (auto operation = weak.lock()) {
            operation->status = completed.status;
            if (completed.status.failed())
              operation->diagnostic = {0, path, "文件读取失败", completed.status};
            operation->bytes = std::move(completed.data);
            operation->read_complete = true;
          }
        },
        work->read_request);
  }
  bool schedule(const std::shared_ptr<operation>& work) {
    work->settled.store(false, std::memory_order_relaxed);
    auto completion = std::make_shared<settlement>();
    completion->work = work;
    const auto submitted = executor->submit(
        {.name = "asset-decode", .scope = scope},
        [completion](const auto& context) {
          auto& task = *completion->work;
          try {
            if (context.stop_requested()) {
              task.status = error::invalid_state;
            } else {
              task.decode();
            }
          } catch (const std::bad_alloc&) {
            task.status = error::out_of_memory;
          } catch (...) {
            task.status = error::internal;
          }
          completion->ran = true;
          return engine::task_outcome{};
        },
        work->task);
    if (submitted == submit_result::full) {
      completion->ran = true;
      work->reading = true;
      return false;
    }
    if (submitted != submit_result::success)
      work->status = error::invalid_state;
    return true;
  }
  ~resource_state() { stop(); }
  void request_stop() noexcept {
    if (stopped)
      return;
    stopped = true;
    if (executor)
      executor->cancel_scope(scope);
    for (const auto& work : scenes) {
      work->descriptions.reset();
      work->observer->status = error::invalid_state;
    }
    for (const auto& work : active) {
      if (work->reading && work->read_request.value != 0)
        (void)files->cancel(work->read_request);
      const std::scoped_lock lock(work->observer->mutex);
      work->observer->status = error::invalid_state;
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
    scenes.clear();
    active.clear();
    cache.clear_loading();
    files.reset();
    return true;
  }
  void stop() noexcept {
    request_stop();
    // 迁移期析构兜底；宿主应先 request_stop 并推进到 finish_stop 完成。
    if (executor) {
      (void)executor->close_scope(scope);
      executor = nullptr;
    }
    (void)finish_stop();
  }
};
std::shared_ptr<engine::files::service> asset_manager::async_files() const noexcept {
  return resource_state_->files;
}
result<asset_identity> asset_manager::identify(std::string_view uri,
                                               std::uint32_t type) const noexcept {
  if (!resource_state_->files || type == 0U)
    return error::invalid_state;
  try {
    std::shared_ptr<const engine::files::source_domain> domain;
    const auto status = resource_state_->files->resolve_source(uri, domain);
    if (status.failed())
      return status;
    return asset_identity{.source = std::move(domain), .uri = std::string(uri), .type = type};
  } catch (...) {
    return error::out_of_memory;
  }
}

asset_manager::asset_manager(std::shared_ptr<engine::files::service> files)
    : resource_state_(std::make_unique<resource_state>()) {
  if (!files)
    throw std::invalid_argument("文件服务不能为空");
  resource_state_->files = std::move(files);
  auto frozen = meta::is_frozen();
  if (!frozen || !*frozen)
    throw std::invalid_argument("资源 Registry 必须冻结");
}
error asset_manager::store_erased(type_id type, std::string_view uri,
                                  std::shared_ptr<const void> value,
                                  std::shared_ptr<const void>& output,
                                  asset_load_options options) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped)
    return error::invalid_state;
  if (!value || validate_uri(uri).failed())
    return error::invalid_argument;
  std::shared_ptr<const engine::files::source_domain> domain;
  auto status = state.files->resolve_source(uri, domain);
  if (status.failed())
    return status;
  const auto parameters = asset_load_parameters(options);
  auto record = cache().acquire({domain, std::string(uri), type, parameters});
  auto stored = cache().load(
      uri, type, [&]() -> result<std::shared_ptr<const void>> { return value; }, domain, true, 0,
      parameters);
  if (!stored)
    return stored.error();
  state.frozen = true;
  record->loading.reset();
  output = std::shared_ptr<const void>(stored->shared(), (*stored)->resource.get());
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
asset_manager::~asset_manager() { stop(); }
resource_cache& asset_manager::cache() noexcept { return resource_state_->cache; }
const resource_cache& asset_manager::cache() const noexcept { return resource_state_->cache; }
std::size_t asset_manager::cached_count() const noexcept { return cache().size(); }
bool asset_manager::stop_requested() const noexcept { return resource_state_->stopped; }
bool asset_manager::loading_configured() const noexcept {
  return resource_state_->executor && !stop_requested();
}
void asset_manager::notify_progress() const noexcept {
  if (resource_state_->notification)
    resource_state_->notification->notify();
}
error asset_manager::configure(task_executor& executor) noexcept {
  return configure(executor, resource_state_->profile, resource_state_->notification);
}
error asset_manager::configure(task_executor& executor, texture_prepare_profile profile,
                               std::shared_ptr<progress_notification> notification) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped)
    return error::invalid_state;
  if (state.executor) {
    return state.executor == &executor && state.profile == profile &&
                   state.notification == notification
               ? error::success
               : error::invalid_state;
  }
  const auto scope = executor.make_scope();
  if (!scope.id)
    return error::invalid_state;
  state.scope = scope;
  state.executor = &executor;
  state.profile = profile;
  state.notification = std::move(notification);
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error asset_manager::attach_render(render_service& render) noexcept try {
  if (resource_state_->stopped)
    return error::invalid_state;
  if (impl_)
    return &impl_->render() == &render ? error::success : error::invalid_state;
  auto next = std::make_unique<detail::asset_construction>(render);
  impl_ = std::move(next);
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
texture_prepare_profile asset_manager::profile() const noexcept { return resource_state_->profile; }
error asset_manager::register_decoder(const std::shared_ptr<const asset_decoder>& loader) noexcept
    try {
  auto& state = *resource_state_;
  if (state.stopped || state.frozen) {
    return error::invalid_state;
  }
  if (!loader) {
    return error::invalid_argument;
  }
  const auto type = loader->target_type();
  // 内置多阶段 Loader 不接受单缓冲区解码器覆盖。
  if (type == runtime_type_of<mesh_data>() || type == runtime_type_of<texture_data>())
    return error::invalid_argument;
  return state.decoders.emplace(type, loader).second ? error::success : error::invalid_argument;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error asset_manager::resolve_decoder(type_id type, decoder_factory fallback,
                                     std::shared_ptr<const asset_decoder>& output) {
  auto& state = *resource_state_;
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
                             bool cache_result) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped) {
    return error::invalid_state;
  }
  if (state.executor == nullptr) {
    return error::not_ready;
  }
  if (validate_uri(uri).failed()) {
    return error::invalid_argument;
  }
  auto task = detail::make_asset_loader(
      type, std::string(uri), options, [&]() -> result<std::shared_ptr<const asset_decoder>> {
        std::shared_ptr<const asset_decoder> decoder;
        auto status = resolve_decoder(type, fallback, decoder);
        return status.failed() ? result<std::shared_ptr<const asset_decoder>>{status} : decoder;
      });
  if (!task)
    return task.error();
  state.frozen = true;
  std::shared_ptr<const engine::files::source_domain> domain;
  const auto identified = state.files->resolve_source(uri, domain);
  if (identified != error::success)
    return identified;
  const auto parameters = asset_load_parameters(options) + (cache_result ? "" : ":payload");
  resource_state::key key{domain, std::string(uri), type, parameters};
  auto entry = cache().acquire(key);
  if (entry->loading && entry->loading->observer.use_count() > 1) {
    output = entry->loading->observer;
    return error::success;
  }
  const auto cached = cache().observe(uri, type, domain, parameters).lock();
  if (cache_result && !options.reload && cached && !cached->is_loading) {
    auto ready = std::make_shared<detail::asset_state>();
    ready->status = error::success;
    ready->source_bytes = cached->source_bytes;
    ready->metrics = cached->metrics;
    ready->value = std::shared_ptr<const void>(cached, cached->resource.get());
    output = std::move(ready);
    return error::success;
  }
  auto work = std::make_shared<resource_state::operation>(std::move(*task));
  work->entry = entry;
  work->cache_result = cache_result;
  work->previous = cached;
  state.active.push_back(work);
  try {
    const auto submitted = state.read(work, uri);
    if (submitted != error::success) {
      state.active.pop_back();
      return submitted;
    }
  } catch (...) {
    state.active.pop_back();
    throw;
  }
  entry->loading = work;
  output = work->observer;
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
error asset_manager::find_loaded_erased(type_id type, std::string_view uri,
                                        std::shared_ptr<const void>& output,
                                        asset_load_options options) const noexcept try {
  if (resource_state_->stopped || !resource_state_->files)
    return error::invalid_state;
  if (validate_uri(uri).failed())
    return error::invalid_argument;
  std::shared_ptr<const engine::files::source_domain> source;
  const auto status = resource_state_->files->resolve_source(uri, source);
  if (status.failed())
    return status;
  const auto entry =
      cache().observe(uri, type, std::move(source), asset_load_parameters(options)).lock();
  if (!entry || entry->is_loading || !entry->resource)
    return error::not_found;
  output = std::shared_ptr<const void>(entry, entry->resource.get());
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<asset_request<scene_instantiation_data>> asset_manager::prepare_scene(std::string_view uri,
                                                                             bool reload) noexcept {
  return prepare_structure(uri, false, reload);
}
result<asset_request<scene_instantiation_data>>
asset_manager::prepare_prefab(std::string_view uri, bool reload) noexcept {
  return prepare_structure(uri, true, reload);
}
result<asset_request<scene_instantiation_data>>
asset_manager::prepare_structure(std::string_view uri, bool prefab, bool reload) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped)
    return error::invalid_state;
  if (!state.executor)
    return error::not_ready;
  if (validate_uri(uri).failed())
    return error::invalid_argument;
  auto work = std::make_shared<resource_state::scene_work>();
  work->descriptions = std::make_unique<detail::scene_description_request>(
      *this, std::string(uri), prefab, scene_prepare_limits{}, reload);
  if (prefab)
    work->prefab_uri = uri;
  state.scenes.push_back(work);
  return asset_request<scene_instantiation_data>{work->observer};
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

void asset_manager::advance() {
  auto& state = *resource_state_;
  if (impl_) {
    (void)impl_->drain();
    impl_->advance(*this);
  }
  if (state.stopped) {
    (void)state.finish_stop();
    return;
  }
  if (state.executor == nullptr) {
    return;
  }
  // 文件完成由宿主统一分发；这里只处理 CPU 解码与资源结果。
  std::vector<task_completion> completions;
  (void)state.executor->poll(state.scope, completions);
  for (const auto& completion : completions)
    for (const auto& work : state.active)
      if (work->task.id == completion.task.id) {
        work->queue_ms += completion.queue_ms;
        work->task = {};
        break;
      }
  for (const auto& completion : completions)
    for (const auto& work : state.scenes)
      if (work->task.id == completion.task.id) {
        work->completed = true;
        work->observer->metrics.decode_ms = completion.execution_ms;
        work->observer->metrics.queue_ms = completion.queue_ms;
      }
  for (auto it = state.scenes.begin(); it != state.scenes.end();) {
    const auto& work = *it;
    if (work->observer.use_count() == 1) {
      if (work->task.id)
        (void)state.executor->cancel(work->task);
      it = state.scenes.erase(it);
      continue;
    }
    if (work->descriptions) {
      work->status = work->descriptions->advance(work->inputs, work->diagnostic);
      if (work->status == error::not_ready) {
        ++it;
        continue;
      }
      work->descriptions.reset();
      if (work->status.failed())
        work->completed = true;
    }
    if (!work->completed && !work->task.id) {
      work->status = error::invalid_state;
      const auto submitted = state.executor->submit(
          {.name = "asset.prepare-scene", .scope = state.scope, .notification = state.notification},
          [work](const task_context& context) {
            work->status =
                prepare_loaded_scene(work->inputs, work->prefab_uri, *work->prepared,
                                     work->diagnostic, [&] { return context.stop_requested(); });
            return task_outcome{.state =
                                    work->status.ok() ? task_state::succeeded : task_state::failed};
          },
          work->task);
      if (submitted != submit_result::success && submitted != submit_result::full) {
        work->status = error::invalid_state;
        work->completed = true;
      }
    }
    if (!work->completed) {
      ++it;
      continue;
    }
    {
      const std::scoped_lock lock(work->observer->mutex);
      work->observer->status = work->status;
      work->observer->diagnostic = {work->diagnostic.byte_offset, work->diagnostic.path,
                                    work->diagnostic.message, work->status};
      if (work->status.ok()) {
        work->observer->value = std::move(work->prepared);
        work->observer->source_bytes = work->inputs.source_bytes;
      }
    }
    it = state.scenes.erase(it);
  }
  for (auto it = state.active.begin(); it != state.active.end();) {
    const auto& work = *it;
    // 丢弃全部观察者后不再提交解码或发布；已经运行的任务自行保活并收尾。
    if (work->observer.use_count() == 1) {
      if (work->task.id != 0U)
        (void)state.executor->cancel(work->task);
      if (work->reading)
        (void)state.files->cancel(work->read_request);
      if (auto entry = work->entry.lock(); entry && entry->loading == work)
        entry->loading.reset();
      it = state.active.erase(it);
      continue;
    }
    if (work->reading) {
      if (!work->read_complete) {
        ++it;
        continue;
      }
      work->reading = false;
      if (work->status == error::success) {
        try {
          if (!state.schedule(work)) {
            ++it;
            continue;
          }
        } catch (const std::bad_alloc&) {
          work->status = error::out_of_memory;
          work->settled.store(true, std::memory_order_release);
        } catch (...) {
          work->status = error::internal;
          work->settled.store(true, std::memory_order_release);
        }
      } else {
        work->settled.store(true, std::memory_order_release);
      }
    }
    if (!work->settled.load(std::memory_order_acquire)) {
      ++it;
      continue;
    }
    if (work->status.ok() && !work->loader->next_uri.empty()) {
      try {
        const auto next_uri = work->loader->next_uri;
        work->status = state.read(work, next_uri);
      } catch (const std::bad_alloc&) {
        work->status = error::out_of_memory;
      } catch (...) {
        work->status = error::internal;
      }
      if (work->status.ok()) {
        ++it;
        continue;
      }
    }
    work->loader->metrics.queue_ms = work->queue_ms;
    if (auto entry = work->entry.lock(); entry && entry->loading == work) {
      entry->loading.reset();
      if (work->status == error::success && work->cache_result) {
        const auto& [domain, uri, type, parameters] = entry->identity;
        auto cached = cache().load(
            uri, type, [&]() -> result<std::shared_ptr<const void>> { return work->value; }, domain,
            true, work->source_bytes, parameters, work->loader->metrics);
        work->status = cached ? error::success : cached.error();
        if (cached)
          work->value = std::shared_ptr<const void>(cached->shared(), (*cached)->resource.get());
      }
    }
    {
      const std::scoped_lock lock(work->observer->mutex);
      work->observer->status = work->status;
      work->observer->diagnostic = std::move(work->diagnostic);
      if (work->status == error::success) {
        work->observer->source_bytes = work->source_bytes;
        work->observer->metrics = work->loader->metrics;
        work->observer->value = std::move(work->value);
      }
    }
    it = state.active.erase(it);
  }
}
void asset_manager::collect_unused() noexcept {
  do {
    cache().release_unused();
  } while (impl_ && impl_->drain() != 0U);
}
void asset_manager::request_stop() noexcept {
  if (impl_)
    impl_->stop_loading();
  resource_state_->request_stop();
}
bool asset_manager::stopped() const noexcept {
  return resource_state_->stopped && !resource_state_->executor && !resource_state_->files;
}
void asset_manager::stop() noexcept {
  request_stop();
  resource_state_->stop();
  collect_unused();
  if (impl_)
    impl_->close();
}

result<asset_request<render_resource_ref>> asset_manager::load(const render_resource_source& source,
                                                               bool reload) try {
  asset_request<render_resource_ref> output;
  if (!impl_ || !loading_configured())
    return error::invalid_state;
  auto reserved = reserve_resource(source);
  if (!reserved)
    return reserved.error();
  auto target = std::move(*reserved);
  auto identity = identify(source.uri, static_cast<std::uint32_t>(source.type));
  if (!identity)
    return identity.error();
  auto entry = cache().acquire({identity->source, source.uri, cache_type(source.type),
                                asset_load_parameters({.texture_profile = profile()})});
  if (entry->loading && entry->loading->observer.use_count() > 1 &&
      entry->loading->observer->status == error::not_ready) {
    output.state_ = entry->loading->observer;
    return output;
  }
  auto state = std::make_shared<detail::asset_state>();
  state->target = target;
  if (!reload && find_loaded(source, profile()).ok()) {
    state->status = error::success;
    output.state_ = std::move(state);
    return output;
  }
  auto work = std::make_shared<detail::asset_construction::pending_load>();
  work->observer = state;
  work->entry = entry;
  switch (source.type) {
  case render_resource_kind::mesh: {
    auto loaded = decode_data<mesh_data>(source.uri, {.reload = reload});
    if (!loaded)
      return loaded.error();
    work->cpu = std::move(*loaded);
    break;
  }
  case render_resource_kind::texture: {
    auto loaded =
        decode_data<texture_data>(source.uri, {.reload = reload, .texture_profile = profile()});
    if (!loaded)
      return loaded.error();
    work->cpu = std::move(*loaded);
    break;
  }
  case render_resource_kind::material: {
    auto loaded = decode_data<material_description>(source.uri, {.reload = reload});
    if (!loaded)
      return loaded.error();
    work->cpu = std::move(*loaded);
    break;
  }
  default:
    return error::invalid_argument;
  }
  impl_->pending.push_back(work);
  entry->loading = work;
  output.state_ = std::move(state);
  return output;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

result<ref<const engine::mesh>> asset_manager::reserve_mesh() noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  return impl_->reserve_mesh();
}
ref<const engine::mesh> asset_manager::find_mesh(rid id) const noexcept {
  return impl_ ? impl_->find_mesh(id) : ref<const engine::mesh>{};
}

result<ref<const engine::texture>> asset_manager::reserve_texture() noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  return impl_->reserve_texture();
}
ref<const engine::texture> asset_manager::find_texture(rid id) const noexcept {
  return impl_ ? impl_->find_texture(id) : ref<const engine::texture>{};
}

result<ref<const engine::material>> asset_manager::reserve_material() noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  return impl_->reserve_material();
}
ref<const engine::material> asset_manager::find_material(rid id) const noexcept {
  return impl_ ? impl_->find_material(id) : ref<const engine::material>{};
}

result<ref<const engine::mesh>> asset_manager::create_mesh(const mesh_view& data) noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  return impl_->create_mesh(data);
}
result<ref<const engine::texture>>
asset_manager::create_texture(const texture_view& data) noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  return impl_->create_texture(data);
}
result<ref<const engine::material>>
asset_manager::create_material(const material_parameters& data,
                               std::array<ref<const engine::texture>, 5> deps) noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  return impl_->create_material(data, std::move(deps));
}

bool asset_manager::owns(const render_resource_ref& value) const noexcept {
  if (auto asset = value.as<engine::mesh>())
    return owns(asset);
  if (auto asset = value.as<engine::texture>())
    return owns(asset);
  if (auto asset = value.as<engine::material>())
    return owns(asset);
  return false;
}

namespace {
render_resource_ref cached_resource(const detail::asset_cache_value& entry) noexcept {
  if (auto asset = entry.as<engine::mesh>())
    return asset;
  if (auto asset = entry.as<engine::texture>())
    return asset;
  if (auto asset = entry.as<engine::material>())
    return asset;
  return {};
}
}

result<render_resource_ref>
asset_manager::find_loaded(const render_resource_source& source,
                           texture_prepare_profile profile) const noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  try {
    auto identity = identify(source.uri, static_cast<std::uint32_t>(source.type));
    if (!identity)
      return identity.error();
    auto domain = std::move(identity->source);
    const auto current = cache()
                             .observe(source.uri, cache_type(source.type), domain,
                                      asset_load_parameters({.texture_profile = this->profile()}))
                             .lock();
    if (!current) {
      return engine::error::not_found;
    }
    if (current->type != cache_type(source.type) || current->is_loading) {
      return engine::error::invalid_argument;
    }
    render_resource_ref target;
    target = cached_resource(*current);
    if (!owns(target)) {
      return engine::error::invalid_handle;
    }
    auto status = engine::error::invalid_argument;
    switch (source.type) {
    case render_resource_kind::texture:
      status = impl_->render().texture_status(resource_rid(target));
      break;
    case render_resource_kind::mesh:
      status = impl_->render().mesh_status(resource_rid(target));
      break;
    case render_resource_kind::material:
      status = impl_->render().material_status(resource_rid(target));
      break;
    case render_resource_kind::invalid:
      break;
    }
    if (status != engine::error::success) {
      return status;
    }
    const auto matches_profile = [&](const engine::texture& resource) {
      const auto rid = detail::texture_resource_access::handle(resource);
      const auto* texture = impl_->render().get_texture(rid);
      return texture &&
             (profile.generation == 0U || texture->manifest.empty() || texture->profile == profile);
    };
    if (source.type == render_resource_kind::texture &&
        !matches_profile(*current->as<engine::texture>())) {
      return engine::error::not_found;
    }
    if (source.type == render_resource_kind::material) {
      const auto material = std::static_pointer_cast<const engine::material>(current->resource);
      for (const auto& texture : material->textures()) {
        if (texture && !matches_profile(*texture)) {
          return engine::error::not_found;
        }
      }
    }
    return cached_resource(*current);
  } catch (...) {
    return engine::error::out_of_memory;
  }
}

result<render_resource_ref>
asset_manager::reserve_resource(const render_resource_source& source) noexcept {
  if (!impl_ || stop_requested())
    return error::invalid_state;
  try {
    if (engine::validate_uri(source.uri) != engine::error::success) {
      return engine::error::invalid_argument;
    }
    auto identified = identify(source.uri, static_cast<std::uint32_t>(source.type));
    if (!identified)
      return identified.error();
    auto identity = std::move(*identified);
    auto cached = cache().load(
        source.uri, cache_type(source.type),
        [&]() -> result<std::shared_ptr<const void>> {
          switch (source.type) {
          case render_resource_kind::mesh: {
            auto value = impl_->reserve_mesh();
            if (!value)
              return value.error();
            detail::mesh_resource_access::set_source(**value, identity);
            return std::shared_ptr<const void>(value->shared());
          }
          case render_resource_kind::texture: {
            auto value = impl_->reserve_texture();
            if (!value)
              return value.error();
            detail::texture_resource_access::set_source(**value, identity);
            return std::shared_ptr<const void>(value->shared());
          }
          case render_resource_kind::material: {
            auto value = impl_->reserve_material();
            if (!value)
              return value.error();
            detail::material_resource_access::set_source(**value, identity);
            return std::shared_ptr<const void>(value->shared());
          }
          default:
            return error::invalid_argument;
          }
        },
        identity.source, false, 0, asset_load_parameters({.texture_profile = profile()}));
    if (!cached)
      return cached.error();
    auto resource = cached_resource(**cached);
    if (!owns(resource))
      return error::invalid_handle;
    return resource;
  } catch (const std::bad_alloc&) {
    return engine::error::out_of_memory;
  } catch (...) {
    return engine::error::internal;
  }
}

bool asset_manager::owns(const ref<const engine::mesh>& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().mesh_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::mesh));
  return identity && *identity == value->source();
}

bool asset_manager::owns(const ref<const engine::texture>& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().texture_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::texture));
  return identity && *identity == value->source();
}

bool asset_manager::owns(const ref<const engine::material>& value) const noexcept {
  if (!impl_)
    return false;
  if (!value || impl_->render().material_status(resource_handle(value)) == error::invalid_handle)
    return false;
  auto identity =
      identify(value->source().uri, static_cast<std::uint32_t>(render_resource_kind::material));
  return identity && *identity == value->source();
}
}
