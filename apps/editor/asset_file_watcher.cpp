// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_file_watcher.h"

#include "uv_loop_access.h"
#include "uv_loop_executor.h"
#include "uv_result.h"

#include <uv.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <future>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace gneiss::editor {
namespace {

[[nodiscard]] std::string path_utf8(const std::filesystem::path& path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char*>(text.data()), text.size()};
}

[[nodiscard]] std::filesystem::path utf8_path(const char* text) {
  const std::string_view view{text == nullptr ? "" : text};
  return std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t*>(view.data()), view.size()));
}

} // namespace

struct asset_file_watcher::implementation final {
  struct directory_watch final {
    uv_fs_event_t handle{};
    implementation* owner = nullptr;
    std::filesystem::path directory;
  };

  explicit implementation(std::size_t capacity) : event_capacity(capacity) {}

  io_internal::uv_loop_executor executor;
  std::filesystem::path source_root;
  std::vector<std::unique_ptr<directory_watch>> watches;
  std::unordered_set<std::string> watched_directories;
  mutable std::mutex event_mutex;
  std::deque<asset_file_event> events;
  std::size_t event_capacity;
  std::atomic_size_t dropped_events = 0U;
  std::atomic_bool running = false;

  void emit(asset_file_event event) noexcept {
    try {
      const std::scoped_lock lock(event_mutex);
      if (event_capacity == 0U) {
        dropped_events.fetch_add(1U, std::memory_order_relaxed);
        return;
      }
      if (events.size() >= event_capacity) {
        events.pop_front();
        dropped_events.fetch_add(1U, std::memory_order_relaxed);
      }
      events.push_back(std::move(event));
    } catch (...) {
      dropped_events.fetch_add(1U, std::memory_order_relaxed);
    }
  }

  [[nodiscard]] result watch_directory(uv_loop_t* loop,
                                       const std::filesystem::path& directory) noexcept {
    try {
      const auto key = path_utf8(directory.lexically_normal());
      if (watched_directories.contains(key)) {
        return result::success;
      }
      auto watch = std::make_unique<directory_watch>();
      watch->owner = this;
      watch->directory = directory;
      auto operation = io_internal::from_uv_status(uv_fs_event_init(loop, &watch->handle));
      if (operation != result::success) {
        return operation;
      }
      watch->handle.data = watch.get();
      unsigned int flags = 0U;
#if defined(_WIN32)
      if (directory == source_root) {
        flags = UV_FS_EVENT_RECURSIVE;
      }
#endif
      operation = io_internal::from_uv_status(
          uv_fs_event_start(&watch->handle, on_file_event, key.c_str(), flags));
      if (operation != result::success) {
        uv_close(reinterpret_cast<uv_handle_t*>(&watch->handle), nullptr);
        return operation;
      }
      watched_directories.insert(key);
      watches.push_back(std::move(watch));
      return result::success;
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::io;
    }
  }

  [[nodiscard]] result refresh_directories(uv_loop_t* loop) noexcept {
    try {
      auto operation = watch_directory(loop, source_root);
      if (operation != result::success) {
        return operation;
      }
      for (const auto& item : std::filesystem::recursive_directory_iterator(source_root)) {
        if (!item.is_directory()) {
          continue;
        }
        operation = watch_directory(loop, std::filesystem::weakly_canonical(item.path()));
        if (operation != result::success) {
          return operation;
        }
      }
      return result::success;
    } catch (...) {
      return result::io;
    }
  }

  static void on_file_event(uv_fs_event_t* handle, const char* filename, int flags,
                            int status) noexcept {
    auto* watch = static_cast<directory_watch*>(handle->data);
    auto* self = watch->owner;
    const auto operation = io_internal::from_uv_status(status);
    if (operation != result::success) {
      self->emit({.kind = asset_file_event_kind::error,
                  .relative_path = watch->directory.lexically_relative(self->source_root),
                  .operation = operation});
      return;
    }

    auto candidate = watch->directory;
    if (filename != nullptr) {
      candidate /= utf8_path(filename);
    }
    auto relative = candidate.lexically_normal().lexically_relative(self->source_root);
    if (relative.empty()) {
      relative = ".";
    }
    self->emit({.kind = (flags & UV_RENAME) != 0 ? asset_file_event_kind::renamed
                                                 : asset_file_event_kind::changed,
                .relative_path = std::move(relative),
                .operation = result::success});
    if ((flags & UV_RENAME) != 0) {
      const auto refresh = self->refresh_directories(handle->loop);
      if (refresh != result::success) {
        self->emit(
            {.kind = asset_file_event_kind::error, .relative_path = ".", .operation = refresh});
      }
    }
  }

  void close_watches() noexcept {
    for (const auto& watch : watches) {
      (void)uv_fs_event_stop(&watch->handle);
      if (uv_is_closing(reinterpret_cast<uv_handle_t*>(&watch->handle)) == 0) {
        uv_close(reinterpret_cast<uv_handle_t*>(&watch->handle), nullptr);
      }
    }
  }
};

asset_file_watcher::asset_file_watcher(std::size_t event_capacity)
    : implementation_(std::make_unique<implementation>(event_capacity)) {}

asset_file_watcher::~asset_file_watcher() {
  if (implementation_) {
    (void)stop();
  }
}

result asset_file_watcher::start(const std::filesystem::path& source_root) noexcept {
  if (!implementation_ || source_root.empty() || implementation_->running.load()) {
    return result::invalid_argument;
  }
  try {
    implementation_->source_root = std::filesystem::weakly_canonical(source_root);
    if (!std::filesystem::is_directory(implementation_->source_root)) {
      return result::not_found;
    }
    auto operation = implementation_->executor.start();
    if (operation != result::success) {
      return operation;
    }
    auto completion = std::make_shared<std::promise<result>>();
    auto completed = completion->get_future();
    operation = io_internal::uv_loop_access::post(
        implementation_->executor, [state = implementation_.get(), completion](uv_loop_t* loop) {
          completion->set_value(state->refresh_directories(loop));
        });
    if (operation == result::success) {
      operation = completed.get();
    }
    if (operation != result::success) {
      (void)io_internal::uv_loop_access::post(
          implementation_->executor,
          [state = implementation_.get()](uv_loop_t*) { state->close_watches(); });
      (void)implementation_->executor.stop();
      implementation_->watches.clear();
      implementation_->watched_directories.clear();
      return operation;
    }
    implementation_->running.store(true, std::memory_order_release);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::io;
  }
}

result asset_file_watcher::stop() noexcept {
  if (!implementation_ || !implementation_->running.exchange(false, std::memory_order_acq_rel)) {
    return result::not_ready;
  }
  const auto posted = io_internal::uv_loop_access::post(
      implementation_->executor,
      [state = implementation_.get()](uv_loop_t*) { state->close_watches(); });
  const auto stopped = implementation_->executor.stop();
  implementation_->watches.clear();
  implementation_->watched_directories.clear();
  return posted != result::success ? posted : stopped;
}

std::size_t asset_file_watcher::poll_events(std::vector<asset_file_event>& output,
                                            std::size_t max_count) noexcept {
  if (!implementation_ || max_count == 0U) {
    return 0U;
  }
  try {
    const std::scoped_lock lock(implementation_->event_mutex);
    const auto count = (std::min)(max_count, implementation_->events.size());
    output.reserve(output.size() + count);
    for (std::size_t index = 0U; index < count; ++index) {
      output.push_back(std::move(implementation_->events.front()));
      implementation_->events.pop_front();
    }
    return count;
  } catch (...) {
    return 0U;
  }
}

bool asset_file_watcher::is_running() const noexcept {
  return implementation_ && implementation_->running.load(std::memory_order_acquire);
}

std::size_t asset_file_watcher::dropped_event_count() const noexcept {
  return implementation_ ? implementation_->dropped_events.load(std::memory_order_relaxed) : 0U;
}

} // namespace gneiss::editor
