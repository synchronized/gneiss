// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "asset_manager.hpp"
#include "engine/core/error.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/registry.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/core/type_id.hpp"
#include "engine/function/resource/detail/asset_loader.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/asset_uri.hpp"
#include "engine/resource/texture/texture_decoder.hpp"
#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <stdexcept>

namespace gneiss::engine {
// 移植示例的类型/位置请求表、合并与只读发布；接入已有执行器，不创建私有线程池。
struct asset_manager::resource_state {
  using key = resource_cache::key;
  struct entry {
    std::uint64_t generation{};
    std::weak_ptr<detail::asset_state> pending;
  };
  struct operation {
    key identity;
    std::uint64_t generation;
    std::shared_ptr<detail::asset_load_task> loader;
    asset_cache_policy policy = asset_cache_policy::retain;
    std::shared_ptr<detail::asset_state> observer{std::make_shared<detail::asset_state>()};
    engine::files::request_id read_request;
    task_handle task;
    double queue_ms{};
    engine::files::bytes bytes;
    std::size_t source_bytes{};
    bool reading{true}, read_complete{};
    std::shared_ptr<const void> value;
    meta::serialization_diagnostic diagnostic;
    // 在途重载保活旧缓存条目；失败后旧内容仍可复用。
    std::shared_ptr<const resource_cache::entry> previous;
    std::atomic_bool settled{false};
    error status{error::not_ready};
    operation(key id, std::uint64_t version, std::shared_ptr<detail::asset_load_task> parser)
        : identity(std::move(id)), generation(version), loader(std::move(parser)) {}
    void decode() {
      const auto started = std::chrono::steady_clock::now();
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
  task_scope scope{};
  bool frozen{}, stopped{};
  std::size_t fallback_limit{};
  std::map<type_id, std::shared_ptr<const asset_decoder>> decoders;
  std::map<key, entry> requests;
  std::vector<std::shared_ptr<operation>> active;

  error read(const std::shared_ptr<operation>& work, std::string_view uri, std::size_t limit) {
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
        uri, {limit},
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
    active.clear();
    requests.clear();
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

asset_manager::asset_manager(std::shared_ptr<engine::files::service> files,
                             std::size_t fallback_limit)
    : resource_state_(std::make_unique<resource_state>()) {
  if (!files)
    throw std::invalid_argument("文件服务不能为空");
  resource_state_->files = std::move(files);
  auto frozen = meta::is_frozen();
  if (fallback_limit == 0 || !frozen || !*frozen)
    throw std::invalid_argument("资源 Registry 必须冻结，输入上限必须非零");
  resource_state_->fallback_limit = fallback_limit;
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
  auto& request = state.requests[{domain, std::string(uri), type, parameters}];
  auto stored = cache_.load(
      uri, type, [&]() -> result<std::shared_ptr<const void>> { return value; }, domain, true, 0,
      parameters, options.cache);
  if (!stored)
    return stored.error();
  state.frozen = true;
  ++request.generation;
  request.pending.reset();
  output = std::shared_ptr<const void>(stored->shared(), (*stored)->resource.get());
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}
asset_manager::~asset_manager() { stop(); }
void asset_manager::attach_task_executor(task_executor& executor) {
  auto& state = *resource_state_;
  if (state.executor && state.executor != &executor)
    throw std::invalid_argument("资产管理器不能切换执行器");
  if ((state.executor == nullptr) && !state.stopped) {
    state.scope = executor.make_scope();
    state.executor = &executor;
  }
}
error asset_manager::register_decoder(const std::shared_ptr<const asset_decoder>& loader) noexcept
    try {
  auto& state = *resource_state_;
  if (state.stopped || state.frozen) {
    return error::invalid_state;
  }
  if (!loader || loader->maximum_bytes() == 0) {
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
  auto decoded = fallback(state.fallback_limit);
  if (!decoded)
    return decoded.error();
  output = std::move(*decoded);
  state.decoders.emplace(type, output);
  return error::success;
}

error asset_manager::request(type_id type, std::string_view uri, asset_load_options options,
                             std::shared_ptr<detail::asset_state>& output,
                             decoder_factory fallback) noexcept try {
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
  if (options.maximum_bytes == 0)
    return error::invalid_argument;
  auto task = detail::make_asset_load_task(
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
  const auto parameters = asset_load_parameters(options);
  resource_state::key key{domain, std::string(uri), type, parameters};
  auto& entry = state.requests[key];
  if (auto pending = entry.pending.lock()) {
    output = std::move(pending);
    return error::success;
  }
  const auto cached = cache_.observe(uri, type, domain, parameters).lock();
  if (!options.reload && cached && !cached->is_loading) {
    auto ready = std::make_shared<detail::asset_state>();
    ready->status = error::success;
    ready->source_bytes = cached->source_bytes;
    ready->metrics = cached->metrics;
    ready->value = std::shared_ptr<const void>(cached, cached->resource.get());
    output = std::move(ready);
    return error::success;
  }
  auto work =
      std::make_shared<resource_state::operation>(key, entry.generation + 1, std::move(*task));
  work->previous = cached;
  work->policy = options.cache;
  state.active.push_back(work);
  try {
    const auto submitted = state.read(work, uri, work->loader->limit);
    if (submitted != error::success) {
      state.active.pop_back();
      return submitted;
    }
  } catch (...) {
    state.active.pop_back();
    throw;
  }
  entry.generation = work->generation;
  entry.pending = work->observer;
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
      cache_.observe(uri, type, std::move(source), asset_load_parameters(options)).lock();
  if (!entry || entry->is_loading || !entry->resource)
    return error::not_found;
  output = std::shared_ptr<const void>(entry, entry->resource.get());
  return error::success;
} catch (const std::bad_alloc&) {
  return error::out_of_memory;
} catch (...) {
  return error::internal;
}

void asset_manager::advance() {
  auto& state = *resource_state_;
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
  for (auto it = state.active.begin(); it != state.active.end();) {
    const auto& work = *it;
    // 丢弃全部观察者后不再提交解码或发布；已经运行的任务自行保活并收尾。
    if (work->observer.use_count() == 1) {
      if (work->task.id != 0U)
        (void)state.executor->cancel(work->task);
      if (work->reading)
        (void)state.files->cancel(work->read_request);
      auto& entry = state.requests.at(work->identity);
      if (entry.generation == work->generation)
        entry.pending.reset();
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
        work->status = state.read(work, next_uri, work->loader->remaining());
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
    auto& entry = state.requests.at(work->identity);
    if (entry.generation == work->generation) {
      entry.pending.reset();
      if (work->status == error::success) {
        const auto& [domain, uri, type, parameters] = work->identity;
        auto cached = cache_.load(
            uri, type, [&]() -> result<std::shared_ptr<const void>> { return work->value; }, domain,
            true, work->source_bytes, parameters, work->policy, work->loader->metrics);
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
  cache_.release_unused();
  auto& state = *resource_state_;
  std::erase_if(state.requests, [&](const auto& item) {
    const auto& [identity, entry] = item;
    const bool active = std::ranges::any_of(
        state.active, [&](const auto& work) { return work->identity == identity; });
    return !active && entry.pending.expired();
  });
}
void asset_manager::request_stop() noexcept { resource_state_->request_stop(); }
bool asset_manager::stopped() const noexcept {
  return resource_state_->stopped && !resource_state_->executor && !resource_state_->files;
}
void asset_manager::stop() noexcept { resource_state_->stop(); }
}
