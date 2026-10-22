// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Granit contributors
// Copyright (c) 2026 Gneiss contributors

#include "asset_manager.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/resource/vfs/asset_source.hpp"
#include "engine/resource/vfs/asset_uri.hpp"
#include <algorithm>
#include <atomic>
#include <map>

namespace gneiss::engine::resource {
namespace {
class input_loader final : public asset_loader {
public:
  [[nodiscard]] std::type_index target_type() const noexcept override {
    return typeid(action_map_asset);
  }
  [[nodiscard]] std::size_t maximum_bytes() const noexcept override {
    return maximum_action_map_bytes;
  }
  result decode(const std::vector<std::byte>& bytes,
                std::shared_ptr<const void>& output) const override {
    auto value = std::make_shared<action_map_asset>();
    const auto status =
        parse_action_map({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, value->map);
    if (status == result::success) {
      output = std::move(value);
    }
    return status;
  }
};
}
// 移植示例的类型/位置请求表、合并与只读发布；接入已有执行器，不创建私有线程池。
struct asset_manager::resource_state {
  using key = std::pair<std::type_index, std::string>;
  struct entry {
    std::uint64_t generation{};
    std::shared_ptr<const void> current;
    std::weak_ptr<detail::asset_state> pending;
  };
  struct operation {
    key identity;
    std::uint64_t generation;
    std::shared_ptr<const asset_loader> loader;
    std::shared_ptr<detail::asset_state> observer{std::make_shared<detail::asset_state>()};
    asset_source source;
    asset_source verification;
    std::shared_ptr<const void> value;
    bool decoded{};
    std::atomic_bool settled{false};
    result status{result::not_ready};
    operation(key id, std::uint64_t version, std::shared_ptr<const asset_loader> parser,
              const virtual_file_system& files)
        : identity(std::move(id)), generation(version), loader(std::move(parser)),
          source(files, identity.second, loader->maximum_bytes()),
          verification(files, identity.second, loader->maximum_bytes()) {}
    void advance() {
      if (!decoded) {
        status = source.advance();
        if (status != result::success) {
          return;
        }
        status = loader->decode(source.bytes(), value);
        if (status != result::success) {
          return;
        }
        if (!value) {
          status = result::internal;
          return;
        }
        decoded = true;
      }
      status = verification.advance();
      if (status == result::success && source.bytes() != verification.bytes()) {
        status = result::io;
      }
    }
  };
  // 执行器丢弃排队任务时也产生终态；所有数据先写入，再 release 发布。
  struct settlement {
    std::shared_ptr<operation> work;
    bool ran{};
    ~settlement() {
      if (!ran) {
        work->status = result::invalid_state;
      }
      work->settled.store(true, std::memory_order_release);
    }
  };
  engine::core::tasks::task_executor* executor{};
  engine::core::tasks::task_scope scope{};
  bool frozen{}, stopped{};
  std::map<std::type_index, std::shared_ptr<const asset_loader>> loaders;
  std::map<key, entry> requests;
  std::vector<std::shared_ptr<operation>> active;

  void schedule(const std::shared_ptr<operation>& work) {
    work->settled.store(false, std::memory_order_relaxed);
    auto completion = std::make_shared<settlement>();
    completion->work = work;
    engine::core::tasks::task_handle handle;
    const auto submitted = executor->submit(
        {.name = "asset-load", .scope = scope},
        [completion](const auto& context) {
          auto& task = *completion->work;
          try {
            if (context.stop_requested()) {
              task.status = result::invalid_state;
            } else {
              task.advance();
            }
          } catch (const std::bad_alloc&) {
            task.status = result::out_of_memory;
          } catch (...) {
            task.status = result::internal;
          }
          completion->ran = true;
          return engine::core::tasks::task_outcome{};
        },
        handle);
    if (submitted != engine::core::tasks::submit_result::success) {
      work->status = result::invalid_state;
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
      work->observer->status = result::invalid_state;
    }
    active.clear();
    requests.clear();
  }
};
asset_manager::asset_manager(virtual_file_system files)
    : resource_state_(std::make_unique<resource_state>()), files_(std::move(files)),
      prefabs_(files_, prefab_cache_) {
  resource_state_->loaders.emplace(typeid(action_map_asset), std::make_shared<input_loader>());
}
asset_manager::~asset_manager() = default;
void asset_manager::attach_task_executor(engine::core::tasks::task_executor& executor) {
  auto& state = *resource_state_;
  if ((state.executor == nullptr) && !state.stopped) {
    state.scope = executor.make_scope();
    state.executor = &executor;
  }
}
result asset_manager::register_loader(const std::shared_ptr<const asset_loader>& loader) noexcept
    try {
  auto& state = *resource_state_;
  if (state.stopped || state.frozen) {
    return result::invalid_state;
  }
  if (!loader || loader->maximum_bytes() == 0) {
    return result::invalid_argument;
  }
  const auto type = loader->target_type();
  return state.loaders.emplace(type, loader).second ? result::success : result::invalid_argument;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result asset_manager::request(std::type_index type, std::string_view uri, bool reload,
                              std::shared_ptr<detail::asset_state>& output) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped) {
    return result::invalid_state;
  }
  if (state.executor == nullptr) {
    return result::not_ready;
  }
  if (validate_uri(uri) != GNEISS_SUCCESS) {
    return result::invalid_argument;
  }
  const auto found = state.loaders.find(type);
  if (found == state.loaders.end()) {
    return result::unsupported;
  }
  state.frozen = true;
  resource_state::key key{type, std::string(uri)};
  auto& entry = state.requests[key];
  if (auto pending = entry.pending.lock()) {
    output = std::move(pending);
    return result::success;
  }
  if (!reload && entry.current) {
    auto ready = std::make_shared<detail::asset_state>();
    ready->status = result::success;
    ready->value = entry.current;
    output = std::move(ready);
    return result::success;
  }
  auto work =
      std::make_shared<resource_state::operation>(key, entry.generation + 1, found->second, files_);
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
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result asset_manager::load_sync_erased(std::type_index type, std::string_view uri,
                                       std::shared_ptr<const void>& output) noexcept try {
  auto& state = *resource_state_;
  if (state.stopped) {
    return result::invalid_state;
  }
  if (validate_uri(uri) != GNEISS_SUCCESS) {
    return result::invalid_argument;
  }
  const auto found = state.loaders.find(type);
  if (found == state.loaders.end()) {
    return result::unsupported;
  }
  state.frozen = true;
  const auto& loader = *found->second;
  std::vector<std::byte> bytes;
  std::vector<std::byte> verified;
  auto status = from_native(files_.read_bounded(uri, loader.maximum_bytes(), bytes));
  if (status != result::success) {
    return status;
  }
  std::shared_ptr<const void> value;
  status = loader.decode(bytes, value);
  if (status != result::success) {
    return status;
  }
  if (!value) {
    return result::internal;
  }
  status = from_native(files_.read_bounded(uri, loader.maximum_bytes(), verified));
  if (status != result::success) {
    return status;
  }
  if (bytes != verified) {
    return result::invalid_state;
  }
  auto& entry = state.requests[{type, std::string(uri)}];
  ++entry.generation;
  entry.pending.reset();
  entry.current = value;
  output = std::move(value);
  return result::success;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result asset_manager::load_input(std::string_view uri, action_map& output) noexcept try {
  std::shared_ptr<const action_map_asset> value;
  const auto status = load_sync(uri, value);
  if (status == result::success) {
    auto copy = value->map;
    output = std::move(copy);
  }
  return status;
} catch (const std::bad_alloc&) {
  return result::out_of_memory;
} catch (...) {
  return result::internal;
}
result asset_manager::request_input(std::string_view uri,
                                    asset_request<action_map_asset>& output) noexcept {
  return load(uri, output, true);
}
void asset_manager::advance_resources() {
  auto& state = *resource_state_;
  if (state.executor == nullptr) {
    return;
  }
  std::vector<engine::core::tasks::task_completion> completions;
  (void)state.executor->poll(state.scope, completions);
  for (auto it = state.active.begin(); it != state.active.end();) {
    const auto& work = *it;
    if (!work->settled.load(std::memory_order_acquire)) {
      ++it;
      continue;
    }
    // 没有观察者时不再推进或发布；不影响同键的独立观察者。
    if (work->observer.use_count() == 1) {
      work->status = result::invalid_state;
    }
    if (work->status == result::not_ready) {
      try {
        state.schedule(work);
        ++it;
        continue;
      } catch (const std::bad_alloc&) {
        work->status = result::out_of_memory;
      } catch (...) {
        work->status = result::internal;
      }
    }
    auto& entry = state.requests.at(work->identity);
    if (entry.generation == work->generation) {
      entry.pending.reset();
      if (work->status == result::success) {
        entry.current = work->value;
      }
    }
    {
      const std::scoped_lock lock(work->observer->mutex);
      work->observer->status = work->status;
      if (work->status == result::success) {
        work->observer->value = std::move(work->value);
      }
    }
    it = state.active.erase(it);
  }
}
void asset_manager::stop_resources() noexcept { resource_state_->stop(); }
}
