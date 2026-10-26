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

namespace gneiss::engine {
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
  std::optional<file_completion<T>> completed;
  bool cancelled{};
};

struct buffer_budget {
  std::size_t reserved{};
};
struct pending_request {
  file_request_id id;
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
  virtual result cancel() = 0;
  virtual bool advance() = 0;
  virtual void deliver() = 0;
};

template <class T> struct typed_request final : pending_request {
  std::shared_ptr<completion_state<T>> state = std::make_shared<completion_state<T>>();
  std::vector<std::shared_ptr<file_backend>> backends;
  std::size_t index{}, backend_base{};
  std::string uri;
  std::function<result(file_backend&, file_callback<T>)> start;
  file_callback<T> callback;
  file_completion<T> outcome;

  void begin() noexcept {
    const auto complete = [shared = state](file_completion<T> value) {
      std::lock_guard lock(shared->mutex);
      if (!shared->completed)
        shared->completed = std::move(value);
    };
    result status;
    try {
      status = start(*backends[index], complete);
    } catch (const std::bad_alloc&) {
      status = result::out_of_memory;
    } catch (...) {
      status = result::internal;
    }
    if (status != result::success)
      complete({status, {}});
  }

  result cancel() override {
    std::lock_guard lock(state->mutex);
    if (state->completed &&
        !(state->completed->status == result::not_found && index + 1 < backends.size()))
      return result::invalid_state;
    state->cancelled = true;
    return result::success;
  }

  bool advance() override {
    bool fallback = false;
    {
      std::lock_guard lock(state->mutex);
      if (!state->completed)
        return false;
      outcome = std::move(*state->completed);
      if (state->cancelled)
        outcome = {result::cancelled, {}};
      fallback = outcome.status == result::not_found && index + 1 < backends.size();
      if (fallback)
        state->completed.reset();
    }
    if (fallback) {
      ++index;
      begin();
      return false;
    }
    if constexpr (std::is_same_v<T, directory_page>) {
      if (outcome.status == result::success && !outcome.data.next_cursor.empty()) {
        try {
          outcome.data.next_cursor =
              std::to_string(index + backend_base) + "\n" + uri + "\n" + outcome.data.next_cursor;
        } catch (const std::bad_alloc&) {
          outcome.status = result::out_of_memory;
        } catch (...) {
          outcome.status = result::internal;
        }
      }
    }
    if (outcome.status != result::success)
      outcome.data = {};
    return true;
  }

  void deliver() override { callback(std::move(outcome)); }
};
} // namespace

struct file_service::implementation {
  file_service_options options;
  std::shared_ptr<buffer_budget> budget = std::make_shared<buffer_budget>();
  std::thread::id owner = std::this_thread::get_id();
  using mounted_files = mount_description;
  std::vector<mounted_files> mounts;
  std::vector<std::shared_ptr<file_backend>> backends;
  std::vector<std::unique_ptr<pending_request>> pending;
  completion_callback on_shutdown;
  bool frozen{}, closing{}, dispatching{};

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
  result submit(std::string_view uri, bool writing, file_callback<T> callback,
                file_request_id& output, Start start, bool exact_mount = false,
                std::optional<std::size_t> reader_index = {},
                std::size_t reserve_bytes = 0) noexcept {
    output = {};
    if (!on_owner() || closing)
      return result::invalid_state;
    if (!callback || !detail::valid_file_uri(uri))
      return result::invalid_argument;
    if (reserve_bytes > options.max_buffer_bytes)
      return result::invalid_argument;
    if (reserve_bytes > options.max_buffer_bytes - budget->reserved)
      return result::not_ready;
    if (pending.size() >= options.max_requests)
      return result::not_ready;
    const auto* mount = resolve(uri);
    if (!mount)
      return result::not_found;
    if (exact_mount && uri != mount->prefix)
      return result::invalid_argument;
    if (writing && !mount->writer)
      return result::unsupported;
    if (!writing && mount->readers.empty())
      return result::unsupported;
    try {
      auto request = std::make_unique<typed_request<T>>();
      request->budget = budget;
      request->reserved = reserve_bytes;
      budget->reserved += reserve_bytes;
      if (reader_index && *reader_index >= mount->readers.size())
        return result::invalid_argument;
      request->backends = writing        ? std::vector{mount->writer}
                          : reader_index ? std::vector{mount->readers[*reader_index]}
                                         : mount->readers;
      if constexpr (std::is_same_v<T, directory_page>)
        request->uri = uri;
      request->backend_base = reader_index.value_or(0);
      request->callback = std::move(callback);
      request->start = [path = std::string(uri.substr(mount->prefix.size())),
                        start = std::move(start)](file_backend& backend,
                                                  file_callback<T> done) mutable {
        return start(backend, path, std::move(done));
      };
      request->id.value = allocate_request();
      if (request->id.value == 0)
        return result::invalid_state;
      auto* accepted = request.get();
      pending.push_back(std::move(request));
      frozen = true;
      output = accepted->id;
      accepted->begin();
      return result::success;
    } catch (const std::bad_alloc&) {
      return result::out_of_memory;
    } catch (...) {
      return result::internal;
    }
  }
};

file_service::file_service(file_service_options options)
    : impl_(std::make_unique<implementation>()) {
  impl_->options = options;
}
file_service::~file_service() = default;
result
file_service::resolve_source(std::string_view uri,
                             std::shared_ptr<const file_source_domain>& output) const noexcept {
  output.reset();
  if (!impl_->on_owner() || impl_->closing)
    return result::invalid_state;
  if (!detail::valid_file_uri(uri))
    return result::invalid_argument;
  const auto* entry = impl_->resolve(uri);
  if (!entry)
    return result::not_found;
  output = entry->source;
  return result::success;
}

result file_service::mount(mount_description description) noexcept {
  if (!impl_->on_owner() || impl_->frozen || impl_->closing)
    return result::invalid_state;
  if (!description.prefix.ends_with('/') || !detail::valid_file_uri(description.prefix) ||
      (description.readers.empty() && !description.writer) ||
      std::ranges::any_of(description.readers, [](const auto& value) { return !value; }))
    return result::invalid_argument;
  if (std::ranges::any_of(impl_->mounts,
                          [&](const auto& entry) { return entry.prefix == description.prefix; }))
    return result::invalid_state;
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
      description.source = std::make_shared<const file_source_domain>();
    impl_->mounts.push_back(std::move(description));
    impl_->backends = std::move(backends);
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result file_service::read(std::string_view uri, read_options options,
                          file_callback<file_bytes> callback, file_request_id& request) noexcept {
  request = {};
  if (options.max_bytes == 0 || options.max_bytes > impl_->options.max_file_bytes)
    return result::invalid_argument;
  return impl_->submit<file_bytes>(
      uri, false, std::move(callback), request,
      [options](file_backend& backend, std::string_view path, auto done) {
        return backend.read(path, options, std::move(done));
      },
      false, {}, options.max_bytes);
}
result file_service::read_range(std::string_view uri, file_range range,
                                file_callback<file_bytes> callback,
                                file_request_id& request) noexcept {
  request = {};
  if (range.size > impl_->options.max_file_bytes ||
      range.offset > std::numeric_limits<std::uint64_t>::max() - range.size)
    return result::invalid_argument;
  return impl_->submit<file_bytes>(
      uri, false, std::move(callback), request,
      [range](file_backend& backend, std::string_view path, auto done) {
        return backend.read_range(path, range, std::move(done));
      },
      false, {}, range.size);
}
result file_service::write(std::string_view uri, file_bytes bytes, write_options options,
                           completion_callback callback, file_request_id& request) noexcept {
  request = {};
  if (!callback || bytes.size() > impl_->options.max_file_bytes ||
      (options.mode != write_mode::replace && options.mode != write_mode::create_new))
    return result::invalid_argument;
  const auto reserve_bytes = bytes.size();
  try {
    return impl_->submit<file_unit>(
        uri, true,
        [callback = std::move(callback)](file_completion<file_unit> value) {
          callback(value.status);
        },
        request,
        [bytes = std::move(bytes), options](file_backend& backend, std::string_view path,
                                            auto done) mutable {
          return backend.write(path, std::move(bytes), options,
                               [done = std::move(done)](result status) { done({status, {}}); });
        },
        false, {}, reserve_bytes);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
result file_service::stat(std::string_view uri, file_callback<file_info> callback,
                          file_request_id& request) noexcept {
  return impl_->submit<file_info>(uri, false, std::move(callback), request,
                                  [](file_backend& backend, std::string_view path, auto done) {
                                    return backend.stat(path, std::move(done));
                                  });
}
result file_service::list(std::string_view uri, list_options options,
                          file_callback<directory_page> callback,
                          file_request_id& request) noexcept {
  request = {};
  if (!impl_->on_owner() || impl_->closing)
    return result::invalid_state;
  if (options.max_entries == 0 || options.max_entries > 1024)
    return result::invalid_argument;
  std::optional<std::size_t> reader_index;
  if (!options.cursor.empty()) {
    const auto first = options.cursor.find('\n');
    const auto second =
        first == std::string::npos ? std::string::npos : options.cursor.find('\n', first + 1);
    if (second == std::string::npos ||
        std::string_view(options.cursor).substr(first + 1, second - first - 1) != uri)
      return result::invalid_argument;
    std::size_t index{};
    const auto parsed =
        std::from_chars(options.cursor.data(), options.cursor.data() + first, index);
    if (parsed.ec != std::errc{} || parsed.ptr != options.cursor.data() + first)
      return result::invalid_argument;
    reader_index = index;
    options.cursor.erase(0, second + 1);
  }
  return impl_->submit<directory_page>(
      uri, false, std::move(callback), request,
      [options = std::move(options)](file_backend& backend, std::string_view path, auto done) {
        return backend.list(path, options, std::move(done));
      },
      false, reader_index);
}
result file_service::flush(std::string_view mount_point, completion_callback callback,
                           file_request_id& request) noexcept {
  request = {};
  if (!callback)
    return result::invalid_argument;
  try {
    return impl_->submit<file_unit>(
        mount_point, true,
        [callback = std::move(callback)](file_completion<file_unit> value) {
          callback(value.status);
        },
        request,
        [](file_backend& backend, std::string_view, auto done) {
          return backend.flush([done = std::move(done)](result status) { done({status, {}}); });
        },
        true);
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
result file_service::cancel(file_request_id request) noexcept {
  if (!impl_->on_owner())
    return result::invalid_state;
  const auto found = std::ranges::find_if(
      impl_->pending, [request](const auto& value) { return value->id == request; });
  return found == impl_->pending.end() ? result::invalid_handle : (*found)->cancel();
}
result file_service::dispatch_completions(std::size_t max_callbacks) {
  if (!impl_->on_owner() || impl_->dispatching)
    return result::invalid_state;
  if (max_callbacks == 0)
    return result::success;
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
    callback(result::success);
  }
  return result::success;
}
result file_service::shutdown(completion_callback callback) noexcept {
  if (!impl_->on_owner() || impl_->closing)
    return result::invalid_state;
  if (!callback)
    return result::invalid_argument;
  impl_->closing = true;
  impl_->on_shutdown = std::move(callback);
  for (const auto& request : impl_->pending)
    static_cast<void>(request->cancel());
  return result::success;
}

} // namespace gneiss::engine
