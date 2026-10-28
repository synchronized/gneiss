// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/platform/files/file_service.hpp"

#include "engine/platform/files/file_backend.hpp"
#include "engine/platform/files/file_path.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace gneiss::engine::files {
namespace {
std::atomic_uint64_t next_request{1};
struct file_unit {};

std::uint64_t allocate_request() noexcept {
  auto value = next_request.load(std::memory_order_relaxed);
  while (value != std::numeric_limits<std::uint64_t>::max()) {
    if (next_request.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
      return value;
  }
  return 0;
}

template <class T> struct completion_state {
  std::mutex mutex;
  std::optional<completion<T>> completed;
  bool cancelled{};
};

struct buffer_budget {
  std::size_t reserved{};
};
struct pending_request {
  request_id id;
  std::shared_ptr<buffer_budget> budget;
  std::size_t reserved{};
  void release_budget() noexcept {
    if (budget) {
      budget->reserved -= reserved;
      reserved = 0;
      budget.reset();
    }
  }
  virtual ~pending_request() { release_budget(); }
  virtual error cancel() = 0;
  virtual bool advance() = 0;
  virtual void deliver() = 0;
};

template <class T> struct typed_request final : pending_request {
  std::shared_ptr<completion_state<T>> state = std::make_shared<completion_state<T>>();
  std::vector<std::shared_ptr<backend>> backends;
  std::size_t index{}, backend_base{};
  std::string uri;
  std::function<error(backend&, files::callback<T>)> start;
  files::callback<T> callback;
  std::function<void()> abort;
  completion<T> outcome;

  void begin() noexcept {
    const auto complete = [shared = state](completion<T> value) {
      std::lock_guard lock(shared->mutex);
      if (!shared->completed)
        shared->completed = std::move(value);
    };
    error status;
    try {
      status = start(*backends[index], complete);
    } catch (const std::bad_alloc&) {
      status = error::out_of_memory;
    } catch (...) {
      status = error::internal;
    }
    if (status != error::success)
      complete({status, {}});
  }

  error cancel() override {
    {
      std::lock_guard lock(state->mutex);
      if (state->completed &&
          !(state->completed->status == error::not_found && index + 1 < backends.size()))
        return error::invalid_state;
      state->cancelled = true;
    }
    // 关闭来源可能内联完成；不得持有完成状态锁。
    if (abort)
      abort();
    return error::success;
  }

  bool advance() override {
    bool fallback = false;
    std::shared_ptr<backend_stream> cancelled_source;
    {
      std::lock_guard lock(state->mutex);
      if (!state->completed)
        return false;
      outcome = std::move(*state->completed);
      if (state->cancelled) {
        // 取消打开后仍可能收到来源；不能只丢弃引用，后端也可能持有它。
        if constexpr (std::is_same_v<T, std::shared_ptr<backend_stream>>) {
          cancelled_source = std::move(outcome.data);
        }
        outcome = {error::cancelled, {}};
      }
      fallback = outcome.status == error::not_found && index + 1 < backends.size();
      if (fallback)
        state->completed.reset();
    }
    if (cancelled_source)
      cancelled_source->close();
    if (fallback) {
      ++index;
      begin();
      return false;
    }
    if constexpr (std::is_same_v<T, directory_page>) {
      if (outcome.status == error::success && !outcome.data.next_cursor.empty()) {
        try {
          outcome.data.next_cursor =
              std::to_string(index + backend_base) + "\n" + uri + "\n" + outcome.data.next_cursor;
        } catch (const std::bad_alloc&) {
          outcome.status = error::out_of_memory;
        } catch (...) {
          outcome.status = error::internal;
        }
      }
    }
    if (outcome.status != error::success)
      outcome.data = {};
    return true;
  }

  void deliver() override { callback(std::move(outcome)); }
};
} // namespace

struct service::implementation {
  service_options options;
  std::shared_ptr<buffer_budget> budget = std::make_shared<buffer_budget>();
  std::thread::id owner = std::this_thread::get_id();
  using mounted_files = mount_description;
  std::vector<mounted_files> mounts;
  std::vector<std::shared_ptr<backend>> backends;
  std::vector<std::unique_ptr<pending_request>> pending;
  completion_callback on_shutdown;
  bool frozen{}, closing{}, dispatching{};
  std::vector<std::weak_ptr<backend_stream>> streams;
  void close_streams() noexcept {
    for (const auto& weak : streams)
      if (auto stream = weak.lock())
        stream->close();
    streams.clear();
  }
  ~implementation() { close_streams(); }

  bool on_owner() const noexcept { return owner == std::this_thread::get_id(); }
  const mounted_files* resolve(std::string_view uri) const noexcept {
    const mounted_files* selected = nullptr;
    for (const auto& mount : mounts) {
      if (uri.starts_with(mount.prefix) &&
          (!selected || mount.prefix.size() > selected->prefix.size()))
        selected = &mount;
    }
    return selected;
  }

  template <class T, class Start>
  error submit(std::string_view uri, bool writing, files::callback<T> callback, request_id& output,
               Start start, bool exact_mount = false, std::optional<std::size_t> reader_index = {},
               std::size_t reserve_bytes = 0, std::function<void()> abort = {}) noexcept {
    output = {};
    if (!on_owner() || closing)
      return error::invalid_state;
    if (!callback || !detail::valid_file_uri(uri))
      return error::invalid_argument;
    if (reserve_bytes > options.max_buffer_bytes)
      return error::invalid_argument;
    if (reserve_bytes > options.max_buffer_bytes - budget->reserved)
      return error::not_ready;
    if (pending.size() >= options.max_requests)
      return error::not_ready;
    const auto* mount = resolve(uri);
    if (!mount)
      return error::not_found;
    if (exact_mount && uri != mount->prefix)
      return error::invalid_argument;
    if (writing && !mount->writer)
      return error::unsupported;
    if (!writing && mount->readers.empty())
      return error::unsupported;
    try {
      auto request = std::make_unique<typed_request<T>>();
      request->budget = budget;
      request->reserved = reserve_bytes;
      budget->reserved += reserve_bytes;
      if (reader_index && *reader_index >= mount->readers.size())
        return error::invalid_argument;
      request->backends = writing        ? std::vector{mount->writer}
                          : reader_index ? std::vector{mount->readers[*reader_index]}
                                         : mount->readers;
      if constexpr (std::is_same_v<T, directory_page>)
        request->uri = uri;
      request->backend_base = reader_index.value_or(0);
      request->callback = std::move(callback);
      request->abort = std::move(abort);
      request->start = [path = std::string(uri.substr(mount->prefix.size())),
                        start = std::move(start)](backend& backend,
                                                  files::callback<T> done) mutable {
        return start(backend, path, std::move(done));
      };
      request->id.value = allocate_request();
      if (request->id.value == 0)
        return error::invalid_state;
      auto* accepted = request.get();
      pending.push_back(std::move(request));
      frozen = true;
      output = accepted->id;
      accepted->begin();
      return error::success;
    } catch (const std::bad_alloc&) {
      return error::out_of_memory;
    } catch (...) {
      return error::internal;
    }
  }
};

service::service(service_options options) : impl_(std::make_unique<implementation>()) {
  impl_->options = options;
}
service::~service() = default;
error service::resolve_source(std::string_view uri,
                              std::shared_ptr<const source_domain>& output) const noexcept {
  output.reset();
  if (!impl_->on_owner() || impl_->closing)
    return error::invalid_state;
  if (!detail::valid_file_uri(uri))
    return error::invalid_argument;
  const auto* entry = impl_->resolve(uri);
  if (!entry)
    return error::not_found;
  output = entry->source;
  return error::success;
}

error service::mount(mount_description description) noexcept {
  if (!impl_->on_owner() || impl_->frozen || impl_->closing)
    return error::invalid_state;
  if (!description.prefix.ends_with('/') || !detail::valid_file_uri(description.prefix) ||
      (description.readers.empty() && !description.writer) ||
      std::ranges::any_of(description.readers, [](const auto& value) { return !value; }))
    return error::invalid_argument;
  if (std::ranges::any_of(impl_->mounts,
                          [&](const auto& entry) { return entry.prefix == description.prefix; }))
    return error::invalid_state;
  try {
    // 先构造新表，分配失败不改变原挂载。
    auto backends = impl_->backends;
    const auto append = [&](const auto& backend) {
      if (backend && std::ranges::find(backends, backend) == backends.end())
        backends.push_back(backend);
    };
    for (const auto& backend : description.readers)
      append(backend);
    append(description.writer);
    if (!description.source)
      description.source = std::make_shared<const source_domain>();
    impl_->mounts.push_back(std::move(description));
    impl_->backends = std::move(backends);
    return error::success;
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}

error service::read(std::string_view uri, read_options options,
                    files::callback<files::bytes> callback, request_id& request) noexcept {
  request = {};
  if (options.max_bytes == 0 || options.max_bytes > impl_->options.max_file_bytes)
    return error::invalid_argument;
  return impl_->submit<files::bytes>(
      uri, false, std::move(callback), request,
      [options](backend& backend, std::string_view path, auto done) {
        return backend.read(path, options, std::move(done));
      },
      false, {}, options.max_bytes);
}
error service::read_range(std::string_view uri, range range, files::callback<files::bytes> callback,
                          request_id& request) noexcept {
  request = {};
  if (range.size > impl_->options.max_file_bytes ||
      range.offset > std::numeric_limits<std::uint64_t>::max() - range.size)
    return error::invalid_argument;
  return impl_->submit<files::bytes>(
      uri, false, std::move(callback), request,
      [range](backend& backend, std::string_view path, auto done) {
        return backend.read_range(path, range, std::move(done));
      },
      false, {}, range.size);
}
error service::open_stream_source(std::string_view uri, std::size_t block_bytes,
                                  files::callback<std::shared_ptr<backend_stream>> callback,
                                  request_id& request) noexcept try {
  request = {};
  if (!callback || !block_bytes || block_bytes > impl_->options.max_file_bytes ||
      block_bytes > impl_->options.max_buffer_bytes / 2)
    return error::invalid_argument;
  return impl_->submit<std::shared_ptr<backend_stream>>(
      uri, false,
      [this, callback = std::move(callback)](auto value) mutable {
        if (value.status.ok()) {
          if (!value.data)
            value.status = error::internal;
          else if (impl_->closing) {
            value.data->close();
            value.status = error::cancelled;
          } else {
            try {
              std::erase_if(impl_->streams, [](const auto& weak) { return weak.expired(); });
              impl_->streams.push_back(value.data);
            } catch (...) {
              value.data->close();
              value.status = error::out_of_memory;
            }
          }
        }
        if (value.status.failed())
          value.data.reset();
        callback(std::move(value));
      },
      request,
      [](backend& backend, std::string_view path, auto done) {
        return backend.open_stream(path, std::move(done));
      });
} catch (...) {
  return error::out_of_memory;
}

error service::read_stream_source(std::string_view uri, std::shared_ptr<backend_stream> source,
                                  std::size_t block_bytes, files::callback<block> callback,
                                  request_id& request) noexcept try {
  request = {};
  if (!callback || !source || !block_bytes || block_bytes > impl_->options.max_file_bytes ||
      block_bytes > impl_->options.max_buffer_bytes / 2)
    return error::invalid_argument;
  // 顺序来源在打开时已完成回退选择；后续块固定该来源，不再重新选择后端。
  return impl_->submit<block>(
      uri, false, std::move(callback), request,
      [source, block_bytes](backend&, std::string_view, auto done) {
        return source->read_next(block_bytes, std::move(done));
      },
      false, 0, block_bytes * 2, [source] { source->close(); });
} catch (...) {
  return error::out_of_memory;
}

error service::write(std::string_view uri, files::bytes bytes, write_options options,
                     completion_callback callback, request_id& request) noexcept {
  request = {};
  if (!callback || bytes.size() > impl_->options.max_file_bytes ||
      (options.mode != write_mode::replace && options.mode != write_mode::create_new))
    return error::invalid_argument;
  const auto reserve_bytes = bytes.size();
  try {
    return impl_->submit<file_unit>(
        uri, true,
        [callback = std::move(callback)](completion<file_unit> value) { callback(value.status); },
        request,
        [bytes = std::move(bytes), options](backend& backend, std::string_view path,
                                            auto done) mutable {
          return backend.write(path, std::move(bytes), options,
                               [done = std::move(done)](error status) { done({status, {}}); });
        },
        false, {}, reserve_bytes);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}
error service::stat(std::string_view uri, files::callback<files::info> callback,
                    request_id& request) noexcept {
  return impl_->submit<files::info>(uri, false, std::move(callback), request,
                                    [](backend& backend, std::string_view path, auto done) {
                                      return backend.stat(path, std::move(done));
                                    });
}
error service::list(std::string_view uri, list_options options,
                    files::callback<directory_page> callback, request_id& request) noexcept {
  request = {};
  if (!impl_->on_owner() || impl_->closing)
    return error::invalid_state;
  if (options.max_entries == 0 || options.max_entries > 1024)
    return error::invalid_argument;
  std::optional<std::size_t> reader_index;
  if (!options.cursor.empty()) {
    const auto first = options.cursor.find('\n');
    const auto second =
        first == std::string::npos ? std::string::npos : options.cursor.find('\n', first + 1);
    if (second == std::string::npos ||
        std::string_view(options.cursor).substr(first + 1, second - first - 1) != uri)
      return error::invalid_argument;
    std::size_t index{};
    const auto parsed =
        std::from_chars(options.cursor.data(), options.cursor.data() + first, index);
    if (parsed.ec != std::errc{} || parsed.ptr != options.cursor.data() + first)
      return error::invalid_argument;
    reader_index = index;
    options.cursor.erase(0, second + 1);
  }
  return impl_->submit<directory_page>(
      uri, false, std::move(callback), request,
      [options = std::move(options)](backend& backend, std::string_view path, auto done) {
        return backend.list(path, options, std::move(done));
      },
      false, reader_index);
}
error service::flush(std::string_view mount_point, completion_callback callback,
                     request_id& request) noexcept {
  request = {};
  if (!callback)
    return error::invalid_argument;
  try {
    return impl_->submit<file_unit>(
        mount_point, true,
        [callback = std::move(callback)](completion<file_unit> value) { callback(value.status); },
        request,
        [](backend& backend, std::string_view, auto done) {
          return backend.flush([done = std::move(done)](error status) { done({status, {}}); });
        },
        true);
  } catch (const std::bad_alloc&) {
    return error::out_of_memory;
  } catch (...) {
    return error::internal;
  }
}
error service::cancel(request_id request) noexcept {
  if (!impl_->on_owner())
    return error::invalid_state;
  const auto found = std::ranges::find_if(
      impl_->pending, [request](const auto& value) { return value->id == request; });
  return found == impl_->pending.end() ? error::invalid_handle : (*found)->cancel();
}
error service::dispatch_completions(std::size_t max_callbacks) {
  if (!impl_->on_owner() || impl_->dispatching)
    return error::invalid_state;
  if (max_callbacks == 0)
    return error::success;
  impl_->dispatching = true;
  struct dispatch_guard {
    bool& active;
    ~dispatch_guard() { active = false; }
  } guard{impl_->dispatching};
  for (const auto& backend : impl_->backends)
    backend->poll();
  const auto count = impl_->pending.size();
  std::size_t index = 0, delivered = 0;
  for (std::size_t visited = 0; visited < count && delivered < max_callbacks; ++visited) {
    if (!impl_->pending[index]->advance()) {
      ++index;
      continue;
    }
    auto ready = std::move(impl_->pending[index]);
    impl_->pending.erase(impl_->pending.begin() + static_cast<std::ptrdiff_t>(index));
    ++delivered;
    ready->release_budget();
    ready->deliver();
  }
  if (impl_->closing && impl_->pending.empty() && impl_->on_shutdown && delivered < max_callbacks) {
    auto callback = std::move(impl_->on_shutdown);
    callback(error::success);
  }
  return error::success;
}
error service::shutdown(completion_callback callback) noexcept {
  if (!impl_->on_owner() || impl_->closing)
    return error::invalid_state;
  if (!callback)
    return error::invalid_argument;
  impl_->closing = true;
  impl_->on_shutdown = std::move(callback);
  for (const auto& request : impl_->pending)
    static_cast<void>(request->cancel());
  impl_->close_streams();
  return error::success;
}

} // namespace gneiss::engine::files
