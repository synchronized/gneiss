// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "asset_manager.hpp"
#include "engine/core/meta/type_registration.hpp"
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/core/type_id.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/files/async_read_file_backend.hpp"
#include "engine/resource/resource_registry.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
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
    std::shared_ptr<const asset_decoder> loader;
    std::shared_ptr<detail::asset_state> observer{std::make_shared<detail::asset_state>()};
    file_request_id read_request;
    file_bytes bytes;
    bool reading{true}, read_complete{};
    std::shared_ptr<const void> value;
    gneiss::engine::meta::serialization_diagnostic diagnostic;
    // 在途重载保活旧缓存条目；失败后旧内容仍可复用。
    std::shared_ptr<const resource_cache::entry> previous;
    std::atomic_bool settled{false};
    gneiss::engine::result status{gneiss::engine::result::not_ready};
    operation(key id, std::uint64_t version, std::shared_ptr<const asset_decoder> parser)
        : identity(std::move(id)), generation(version), loader(std::move(parser)) {}
    void decode() {
      status = loader->decode(bytes, value, diagnostic);
      // 解码任务只有一次终态；not_ready 不能变成永久挂起的资产请求。
      if (status == result::not_ready || (status == result::success && !value))
        status = result::internal;
      file_bytes{}.swap(bytes);
    }
  };
  // 执行器丢弃排队任务时也产生终态；所有数据先写入，再 release 发布。
  struct settlement {
    std::shared_ptr<operation> work;
    bool ran{};
    ~settlement() {
      if (!ran) {
        work->status = gneiss::engine::result::invalid_state;
      }
      work->settled.store(true, std::memory_order_release);
    }
  };
  std::shared_ptr<file_service> files;
  bool legacy_sync{};
  gneiss::engine::task_executor* executor{};
  gneiss::engine::task_scope scope{};
  bool frozen{}, stopped{};
  gneiss::engine::meta::registry_ref registry;
  std::size_t fallback_limit{};
  std::map<gneiss::engine::type_id, std::shared_ptr<const asset_decoder>> decoders;
  std::map<key, entry> requests;
  std::vector<std::shared_ptr<operation>> active;

  void schedule(const std::shared_ptr<operation>& work) {
    work->settled.store(false, std::memory_order_relaxed);
    auto completion = std::make_shared<settlement>();
    completion->work = work;
    gneiss::engine::task_handle handle;
    const auto submitted = executor->submit(
        {.name = "asset-decode", .scope = scope},
        [completion](const auto& context) {
          auto& task = *completion->work;
          try {
            if (context.stop_requested()) {
              task.status = gneiss::engine::result::invalid_state;
            } else {
              task.decode();
            }
          } catch (const std::bad_alloc&) {
            task.status = gneiss::engine::result::out_of_memory;
          } catch (...) {
            task.status = gneiss::engine::result::internal;
          }
          completion->ran = true;
          return gneiss::engine::task_outcome{};
        },
        handle);
    if (submitted != gneiss::engine::submit_result::success) {
      work->status = gneiss::engine::result::invalid_state;
    }
  }
  ~resource_state() { stop(); }
  void stop() noexcept {
    stopped = true;
    if (executor != nullptr) {
      (void)executor->close_scope(scope);
      executor = nullptr;
    }
    for (const auto& work : active) {
      if (work->reading && work->read_request.value != 0)
        (void)files->cancel(work->read_request);
      const std::scoped_lock lock(work->observer->mutex);
      work->observer->status = gneiss::engine::result::invalid_state;
    }
    active.clear();
    requests.clear();
    files.reset();
  }
};
std::shared_ptr<file_service> asset_manager::async_files() const noexcept {
  return resource_state_->files;
}
result asset_manager::identify(std::string_view uri, std::uint32_t type,
                               asset_identity& output) const noexcept {
  output = {};
  if (!resource_state_->files || type == 0U)
    return result::invalid_state;
  try {
    std::shared_ptr<const file_source_domain> domain;
    const auto status = resource_state_->files->resolve_source(uri, domain);
    if (status.failed())
      return status;
    output = {.source = std::move(domain), .uri = std::string(uri), .type = type};
    return result::success;
  } catch (...) {
    return result::out_of_memory;
  }
}

asset_manager::asset_manager(virtual_file_system files, gneiss::engine::meta::registry_ref registry,
                             std::size_t fallback_limit)
    : asset_manager(make_file_service(
                        files, file_service_options{.max_requests = 64,
                                                    .max_file_bytes =
                                                        std::numeric_limits<std::size_t>::max()}),
                    std::move(registry), fallback_limit) {
  files_ = std::move(files);
  resource_state_->legacy_sync = true;
}
asset_manager::asset_manager(std::shared_ptr<file_service> files,
                             gneiss::engine::meta::registry_ref registry,
                             std::size_t fallback_limit)
    : resource_state_(std::make_unique<resource_state>()) {
  if (!files)
    throw std::invalid_argument("文件服务不能为空");
  resource_state_->files = std::move(files);
  if (!registry) {
    const auto status = builtin_resource_registry(registry);
    if (status == result::out_of_memory)
      throw std::bad_alloc{};
    if (status.failed())
      throw std::runtime_error("无法创建资源反射注册表");
  }
  bool frozen{};
  if (fallback_limit == 0 || is_frozen(registry, frozen).failed() || !frozen)
    throw std::invalid_argument("资源 Registry 必须冻结，输入上限必须非零");
  resource_state_->registry = std::move(registry);
  resource_state_->fallback_limit = fallback_limit;
}
result asset_manager::store_erased(type_id type, std::string_view uri,
                                   std::shared_ptr<const void> value,
                                   std::shared_ptr<const void>& output) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped)
    return result::invalid_state;
  if (!value || validate_uri(uri).failed())
    return result::invalid_argument;
  std::shared_ptr<const file_source_domain> domain;
  auto status = state.files->resolve_source(uri, domain);
  if (status.failed())
    return status;
  auto& request = state.requests[{domain, std::string(uri), type}];
  asset_ref<const resource_cache::entry> entry;
  status = cache_.load(
      uri, type,
      [&](auto& stored) {
        stored = value;
        return result::success;
      },
      entry, domain, true);
  if (status == result::success) {
    state.frozen = true;
    ++request.generation;
    request.pending.reset();
    output = std::shared_ptr<const void>(entry.shared(), entry->resource.get());
  }
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
asset_manager::~asset_manager() { stop_resources(); }
void asset_manager::attach_task_executor(gneiss::engine::task_executor& executor) {
  auto& state = *resource_state_;
  if (state.executor && state.executor != &executor)
    throw std::invalid_argument("资产管理器不能切换执行器");
  if ((state.executor == nullptr) && !state.stopped) {
    state.scope = executor.make_scope();
    state.executor = &executor;
  }
}
gneiss::engine::result
asset_manager::register_decoder(const std::shared_ptr<const asset_decoder>& loader) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped || state.frozen) {
    return gneiss::engine::result::invalid_state;
  }
  if (!loader || loader->maximum_bytes() == 0) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto type = loader->target_type();
  return state.decoders.emplace(type, loader).second ? gneiss::engine::result::success
                                                     : gneiss::engine::result::invalid_argument;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
result asset_manager::resolve_decoder(type_id type, decoder_factory fallback,
                                      std::shared_ptr<const asset_decoder>& output) {
  auto& state = *resource_state_;
  state.frozen = true;
  const auto found = state.decoders.find(type);
  if (found != state.decoders.end()) {
    output = found->second;
    return result::success;
  }
  const auto status = fallback(state.registry, state.fallback_limit, output);
  if (status == result::success)
    state.decoders.emplace(type, output);
  return status;
}

gneiss::engine::result asset_manager::request(gneiss::engine::type_id type, std::string_view uri,
                                              bool reload,
                                              std::shared_ptr<detail::asset_state>& output,
                                              decoder_factory fallback) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped) {
    return gneiss::engine::result::invalid_state;
  }
  if (state.executor == nullptr) {
    return gneiss::engine::result::not_ready;
  }
  if (validate_uri(uri) != gneiss::engine::result::success) {
    return gneiss::engine::result::invalid_argument;
  }
  std::shared_ptr<const asset_decoder> decoder;
  const auto resolved = resolve_decoder(type, fallback, decoder);
  if (resolved.failed())
    return resolved;
  std::shared_ptr<const file_source_domain> domain;
  const auto identified = state.files->resolve_source(uri, domain);
  if (identified != gneiss::engine::result::success)
    return identified;
  resource_state::key key{domain, std::string(uri), type};
  auto& entry = state.requests[key];
  if (auto pending = entry.pending.lock()) {
    output = std::move(pending);
    return gneiss::engine::result::success;
  }
  const auto cached = cache_.observe(uri, type, domain).lock();
  if (!reload && cached && cached->state == gneiss::engine::resource_state::ready) {
    auto ready = std::make_shared<detail::asset_state>();
    ready->status = gneiss::engine::result::success;
    ready->value = std::shared_ptr<const void>(cached, cached->resource.get());
    output = std::move(ready);
    return gneiss::engine::result::success;
  }
  auto work = std::make_shared<resource_state::operation>(key, entry.generation + 1, decoder);
  work->previous = cached;
  state.active.push_back(work);
  try {
    const auto submitted = state.files->read(
        uri, {decoder->maximum_bytes()},
        [weak = std::weak_ptr(work)](file_completion<file_bytes> completed) {
          // 回调只向仍存活的操作交付字节；停止/销毁后不捕获 manager 或执行器。
          if (auto operation = weak.lock()) {
            operation->status = completed.status;
            operation->bytes = std::move(completed.data);
            operation->read_complete = true;
          }
        },
        work->read_request);
    if (submitted != result::success) {
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
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
result asset_manager::find_loaded_erased(type_id type, std::string_view uri,
                                         std::shared_ptr<const void>& output) const noexcept try {
  if (resource_state_->stopped || !resource_state_->files)
    return result::invalid_state;
  if (validate_uri(uri).failed())
    return result::invalid_argument;
  std::shared_ptr<const file_source_domain> source;
  const auto status = resource_state_->files->resolve_source(uri, source);
  if (status.failed())
    return status;
  const auto entry = cache_.observe(uri, type, std::move(source)).lock();
  if (!entry || entry->state != gneiss::engine::resource_state::ready || !entry->resource)
    return result::not_found;
  output = entry->resource;
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}

gneiss::engine::result asset_manager::load_sync_erased(
    gneiss::engine::type_id type, std::string_view uri, std::shared_ptr<const void>& output,
    decoder_factory fallback, bool reload,
    gneiss::engine::meta::serialization_diagnostic* diagnostic) noexcept try {
  gneiss::engine::meta::serialization_diagnostic local;
  if (diagnostic)
    *diagnostic = {};
  auto& state = *resource_state_;
  if (state.stopped) {
    return gneiss::engine::result::invalid_state;
  }
  if (validate_uri(uri) != gneiss::engine::result::success) {
    return gneiss::engine::result::invalid_argument;
  }
  std::shared_ptr<const asset_decoder> decoder;
  const auto resolved = resolve_decoder(type, fallback, decoder);
  if (resolved.failed())
    return resolved;
  std::shared_ptr<const file_source_domain> domain;
  const auto identified = state.files->resolve_source(uri, domain);
  if (identified != gneiss::engine::result::success)
    return identified;
  const auto& loader = *decoder;
  auto& entry = state.requests[{domain, std::string(uri), type}];
  asset_ref<const resource_cache::entry> cached;
  bool decoded{};
  const auto status = cache_.load(
      uri, type,
      [&](auto& stored) {
        if (!state.legacy_sync)
          return result::unsupported;
        decoded = true;
        std::vector<std::byte> bytes;
        const auto read = files_.read_bounded(uri, loader.maximum_bytes(), bytes);
        return read == gneiss::engine::result::success
                   ? loader.decode(bytes, stored, diagnostic ? *diagnostic : local)
                   : read;
      },
      cached, domain, reload);
  if (status != gneiss::engine::result::success)
    return status;
  if (decoded) {
    ++entry.generation;
    entry.pending.reset();
  }
  output = std::shared_ptr<const void>(cached.shared(), cached->resource.get());
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result asset_manager::load_input(std::string_view uri, action_map& output) noexcept
    try {
  asset_ref<const action_map> value;
  const auto status = load_sync(uri, value, true);
  if (status == gneiss::engine::result::success) {
    auto copy = *value;
    output = std::move(copy);
  }
  return status;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result asset_manager::request_input(std::string_view uri,
                                                    asset_request<action_map>& output) noexcept {
  return load(uri, output, true);
}
void asset_manager::advance_resources() {
  auto& state = *resource_state_;
  if (state.executor == nullptr) {
    return;
  }
  // 文件完成由宿主统一分发；这里只处理 CPU 解码与资源结果。
  std::vector<gneiss::engine::task_completion> completions;
  (void)state.executor->poll(state.scope, completions);
  for (auto it = state.active.begin(); it != state.active.end();) {
    const auto& work = *it;
    // 丢弃全部观察者后不再提交解码或发布；已经运行的任务自行保活并收尾。
    if (work->observer.use_count() == 1) {
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
      if (work->status == result::success) {
        try {
          state.schedule(work);
        } catch (const std::bad_alloc&) {
          work->status = result::out_of_memory;
          work->settled.store(true, std::memory_order_release);
        } catch (...) {
          work->status = result::internal;
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
    auto& entry = state.requests.at(work->identity);
    if (entry.generation == work->generation) {
      entry.pending.reset();
      if (work->status == gneiss::engine::result::success) {
        const auto& [domain, uri, type] = work->identity;
        asset_ref<const resource_cache::entry> cached;
        work->status = cache_.load(
            uri, type,
            [&](auto& stored) {
              stored = work->value;
              return gneiss::engine::result::success;
            },
            cached, domain, true);
        if (work->status == gneiss::engine::result::success) {
          work->value = std::shared_ptr<const void>(cached.shared(), cached->resource.get());
        }
      }
    }
    {
      const std::scoped_lock lock(work->observer->mutex);
      work->observer->status = work->status;
      work->observer->diagnostic = std::move(work->diagnostic);
      if (work->status == gneiss::engine::result::success) {
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
void asset_manager::stop_resources() noexcept { resource_state_->stop(); }
}
