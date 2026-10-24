// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "asset_manager.hpp"
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/core/type_id.hpp"
#include "engine/resource/vfs/asset_source.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <algorithm>
#include <atomic>
#include <map>

namespace gneiss::engine {
namespace {
class input_loader final : public asset_loader {
public:
  [[nodiscard]] gneiss::engine::type_id target_type() const noexcept override {
    return gneiss::engine::runtime_type_of<action_map_asset>();
  }
  [[nodiscard]] std::size_t maximum_bytes() const noexcept override {
    return maximum_action_map_bytes;
  }
  gneiss::engine::result decode(const std::vector<std::byte>& bytes,
                                std::shared_ptr<const void>& output) const override {
    auto value = std::make_shared<action_map_asset>();
    const auto status =
        parse_action_map({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, value->map);
    if (status == gneiss::engine::result::success) {
      output = std::move(value);
    }
    return status;
  }
};
}
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
    std::shared_ptr<const asset_loader> loader;
    std::shared_ptr<detail::asset_state> observer{std::make_shared<detail::asset_state>()};
    asset_source source;
    std::shared_ptr<const void> value;
    // 在途重载保活旧缓存条目；失败后旧内容仍可复用。
    std::shared_ptr<const resource_cache::entry> previous;
    std::atomic_bool settled{false};
    gneiss::engine::result status{gneiss::engine::result::not_ready};
    operation(key id, std::uint64_t version, std::shared_ptr<const asset_loader> parser,
              const virtual_file_system& files)
        : identity(std::move(id)), generation(version), loader(std::move(parser)),
          source(files, std::get<1>(identity), loader->maximum_bytes()) {}
    void advance() {
      status = source.advance();
      if (status != gneiss::engine::result::success) {
        return;
      }
      status = loader->decode(source.bytes(), value);
      if (status == gneiss::engine::result::success && !value) {
        status = gneiss::engine::result::internal;
      }
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
  gneiss::engine::task_executor* executor{};
  gneiss::engine::task_scope scope{};
  bool frozen{}, stopped{};
  std::map<gneiss::engine::type_id, std::shared_ptr<const asset_loader>> loaders;
  std::map<key, entry> requests;
  std::vector<std::shared_ptr<operation>> active;

  void schedule(const std::shared_ptr<operation>& work) {
    work->settled.store(false, std::memory_order_relaxed);
    auto completion = std::make_shared<settlement>();
    completion->work = work;
    gneiss::engine::task_handle handle;
    const auto submitted = executor->submit(
        {.name = "asset-load", .scope = scope},
        [completion](const auto& context) {
          auto& task = *completion->work;
          try {
            if (context.stop_requested()) {
              task.status = gneiss::engine::result::invalid_state;
            } else {
              task.advance();
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
      const std::scoped_lock lock(work->observer->mutex);
      work->observer->status = gneiss::engine::result::invalid_state;
    }
    active.clear();
    requests.clear();
  }
};
asset_manager::asset_manager(virtual_file_system files)
    : resource_state_(std::make_unique<resource_state>()), files_(std::move(files)),
      prefabs_(files_, cache_) {
  resource_state_->loaders.emplace(gneiss::engine::runtime_type_of<action_map_asset>(),
                                   std::make_shared<input_loader>());
}
asset_manager::~asset_manager() { stop_resources(); }
void asset_manager::attach_task_executor(gneiss::engine::task_executor& executor) {
  auto& state = *resource_state_;
  if ((state.executor == nullptr) && !state.stopped) {
    state.scope = executor.make_scope();
    state.executor = &executor;
  }
}
gneiss::engine::result
asset_manager::register_loader(const std::shared_ptr<const asset_loader>& loader) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped || state.frozen) {
    return gneiss::engine::result::invalid_state;
  }
  if (!loader || loader->maximum_bytes() == 0) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto type = loader->target_type();
  return state.loaders.emplace(type, loader).second ? gneiss::engine::result::success
                                                    : gneiss::engine::result::invalid_argument;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result asset_manager::request(gneiss::engine::type_id type, std::string_view uri,
                                              bool reload,
                                              std::shared_ptr<detail::asset_state>& output) noexcept
    try {
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
  const auto found = state.loaders.find(type);
  if (found == state.loaders.end()) {
    return gneiss::engine::result::unsupported;
  }
  state.frozen = true;
  std::shared_ptr<const asset_source_domain> domain;
  const auto identified = files_.resolve_source(uri, domain);
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
  auto work =
      std::make_shared<resource_state::operation>(key, entry.generation + 1, found->second, files_);
  work->previous = cached;
  state.active.push_back(work);
  try {
    state.schedule(work);
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
gneiss::engine::result asset_manager::load_sync_erased(gneiss::engine::type_id type,
                                                       std::string_view uri,
                                                       std::shared_ptr<const void>& output) noexcept
    try {
  auto& state = *resource_state_;
  if (state.stopped) {
    return gneiss::engine::result::invalid_state;
  }
  if (validate_uri(uri) != gneiss::engine::result::success) {
    return gneiss::engine::result::invalid_argument;
  }
  const auto found = state.loaders.find(type);
  if (found == state.loaders.end()) {
    return gneiss::engine::result::unsupported;
  }
  state.frozen = true;
  std::shared_ptr<const asset_source_domain> domain;
  const auto identified = files_.resolve_source(uri, domain);
  if (identified != gneiss::engine::result::success)
    return identified;
  const auto& loader = *found->second;
  auto& entry = state.requests[{domain, std::string(uri), type}];
  asset_ref<const resource_cache::entry> cached;
  const auto status = cache_.load(
      uri, type,
      [&](auto& stored) {
        std::vector<std::byte> bytes;
        const auto read = files_.read_bounded(uri, loader.maximum_bytes(), bytes);
        return read == gneiss::engine::result::success ? loader.decode(bytes, stored) : read;
      },
      cached, domain, true);
  if (status != gneiss::engine::result::success)
    return status;
  ++entry.generation;
  entry.pending.reset();
  output = std::shared_ptr<const void>(cached.shared(), cached->resource.get());
  return gneiss::engine::result::success;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result asset_manager::load_input(std::string_view uri, action_map& output) noexcept
    try {
  asset_ref<const action_map_asset> value;
  const auto status = load_sync(uri, value);
  if (status == gneiss::engine::result::success) {
    auto copy = value->map;
    output = std::move(copy);
  }
  return status;
} catch (const std::bad_alloc&) {
  return gneiss::engine::result::out_of_memory;
} catch (...) {
  return gneiss::engine::result::internal;
}
gneiss::engine::result
asset_manager::request_input(std::string_view uri,
                             asset_request<action_map_asset>& output) noexcept {
  return load(uri, output, true);
}
void asset_manager::advance_resources() {
  auto& state = *resource_state_;
  if (state.executor == nullptr) {
    return;
  }
  std::vector<gneiss::engine::task_completion> completions;
  (void)state.executor->poll(state.scope, completions);
  for (auto it = state.active.begin(); it != state.active.end();) {
    const auto& work = *it;
    if (!work->settled.load(std::memory_order_acquire)) {
      ++it;
      continue;
    }
    // 没有观察者时不再推进或发布；不影响同键的独立观察者。
    if (work->observer.use_count() == 1) {
      work->status = gneiss::engine::result::invalid_state;
    }
    if (work->status == gneiss::engine::result::not_ready) {
      try {
        state.schedule(work);
        ++it;
        continue;
      } catch (const std::bad_alloc&) {
        work->status = gneiss::engine::result::out_of_memory;
      } catch (...) {
        work->status = gneiss::engine::result::internal;
      }
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
