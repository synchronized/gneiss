// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_async_file.hpp"
#include "engine/core/result.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "uv_loop_access.hpp"
#include "uv_result.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace gneiss::engine {
namespace file_detail {
// 只有异步关闭提交本身失败时执行同步兜底，调用点始终在 I/O loop，避免泄漏描述符。
void close_after_submission_failure(int descriptor) noexcept {
  uv_fs_t request{};
  (void)uv_fs_close(nullptr, &request, descriptor, nullptr);
  uv_fs_req_cleanup(&request);
}
struct budget {
  std::mutex mutex;
  std::size_t requests{};
  std::size_t bytes{};
  std::size_t maximum_requests;
  std::size_t maximum_bytes;
  explicit budget(uv_file_limits limits)
      : maximum_requests(limits.requests), maximum_bytes(limits.bytes) {}
};
struct reservation {
  std::shared_ptr<budget> owner;
  std::size_t bytes{};
  bool acquired{};
  bool acquire(std::shared_ptr<budget> source, std::size_t count) {
    owner = std::move(source);
    const std::scoped_lock lock(owner->mutex);
    if (owner->requests >= owner->maximum_requests || count > owner->maximum_bytes - owner->bytes) {
      return false;
    }
    ++owner->requests;
    owner->bytes += count;
    bytes = count;
    acquired = true;
    return true;
  }
  ~reservation() {
    if (acquired) {
      const std::scoped_lock lock(owner->mutex);
      --owner->requests;
      owner->bytes -= bytes;
    }
  }
};
struct receipt {
  std::atomic_bool ready = false;
  gneiss::engine::result status = gneiss::engine::result::not_ready;
  std::mutex mutex;
  std::condition_variable changed;
  void publish(gneiss::engine::result value) noexcept {
    {
      const std::scoped_lock lock(mutex);
      // 已完成的底层失败不能继续伪装为尚未就绪。
      status = value == gneiss::engine::result::not_ready ? gneiss::engine::result::io : value;
      ready.store(true, std::memory_order_release);
    }
    changed.notify_all();
  }
};
struct file {
  uv_loop_t* loop{};
  uv_async_t wakeup{};
  uv_fs_t closing{};
  int descriptor = -1;
  std::uint64_t size{};
  std::shared_ptr<budget> capacity;
  std::shared_ptr<file> lifetime;
  std::mutex mutex;
  std::vector<std::shared_ptr<reading>> queued;
  bool released{};
  bool close_started{};
  std::size_t active_reads{};
  receipt close_receipt;
  gneiss::engine::result close_status = gneiss::engine::result::success;
  void read_finished() noexcept {
    const std::scoped_lock lock(mutex);
    --active_reads;
    if (released && active_reads == 0)
      (void)uv_async_send(&wakeup);
  }
  static void dispatch(uv_async_t* handle) noexcept;
  static void closed(uv_fs_t* request) noexcept {
    auto* self = static_cast<file*>(request->data);
    if (request->result < 0)
      self->close_status = from_uv_status(static_cast<int>(request->result));
    uv_fs_req_cleanup(request);
    uv_close(reinterpret_cast<uv_handle_t*>(&self->wakeup), [](uv_handle_t* handle) {
      auto* state = static_cast<file*>(handle->data);
      state->close_receipt.publish(state->close_status);
      auto lifetime = std::move(state->lifetime);
    });
  }
};
struct reading : receipt {
  uv_fs_t request{};
  std::shared_ptr<const uv_async_file> source;
  std::shared_ptr<reading> lifetime;
  reservation reserved;
  std::vector<std::byte> bytes;
  std::uint64_t offset{};
  std::size_t consumed{};
  void finish(gneiss::engine::result value) noexcept {
    source->state_->read_finished();
    source.reset();
    publish(value);
    lifetime.reset();
  }
  void submit() noexcept {
    auto buffer = uv_buf_init(
        reinterpret_cast<char*>(bytes.data() + consumed),
        static_cast<unsigned int>(std::min(
            bytes.size() - consumed, static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    request.data = this;
    const auto code =
        uv_fs_read(source->state_->loop, &request, source->state_->descriptor, &buffer, 1U,
                   static_cast<std::int64_t>(offset + consumed), completed);
    if (code < 0) {
      uv_fs_req_cleanup(&request);
      finish(from_uv_status(code));
    }
  }
  static void completed(uv_fs_t* request) noexcept {
    auto* self = static_cast<reading*>(request->data);
    auto guard = self->lifetime;
    const auto count = request->result;
    uv_fs_req_cleanup(request);
    if (count <= 0 || std::cmp_greater(count, self->bytes.size() - self->consumed)) {
      self->finish(count < 0 ? from_uv_status(static_cast<int>(count))
                             : gneiss::engine::result::io);
      return;
    }
    self->consumed += static_cast<std::size_t>(count);
    if (self->consumed == self->bytes.size()) {
      self->finish(gneiss::engine::result::success);
    } else {
      self->submit();
    }
  }
};
void file::dispatch(uv_async_t* handle) noexcept {
  auto* self = static_cast<file*>(handle->data);
  std::vector<std::shared_ptr<reading>> pending;
  bool close = false;
  {
    const std::scoped_lock lock(self->mutex);
    pending.swap(self->queued);
    close = self->released && self->active_reads == 0 && !self->close_started;
    if (close)
      self->close_started = true;
  }
  for (auto& read : pending) {
    read->lifetime = read;
    if (read->bytes.empty()) {
      read->finish(gneiss::engine::result::success);
    } else {
      read->submit();
    }
  }
  if (close) {
    self->closing.data = self;
    const auto code = uv_fs_close(self->loop, &self->closing, self->descriptor, closed);
    if (code < 0) {
      self->close_status = from_uv_status(code);
      close_after_submission_failure(self->descriptor);
      closed(&self->closing);
    }
  }
}
struct opening : receipt {
  uv_fs_t request{};
  uv_loop_t* loop{};
  std::string path;
  std::shared_ptr<opening> lifetime;
  reservation reserved;
  std::shared_ptr<uv_async_file> output;
  int descriptor = -1;
  gneiss::engine::result failure = gneiss::engine::result::internal;
  void fail(gneiss::engine::result value) noexcept {
    failure = value;
    if (descriptor < 0) {
      publish(value);
      lifetime.reset();
      return;
    }
    request.data = this;
    const auto code = uv_fs_close(loop, &request, descriptor, closed);
    if (code < 0) {
      close_after_submission_failure(descriptor);
      closed(&request);
    }
  }
  static void closed(uv_fs_t* request) noexcept {
    auto* self = static_cast<opening*>(request->data);
    auto guard = self->lifetime;
    uv_fs_req_cleanup(request);
    self->publish(self->failure);
    self->lifetime.reset();
  }
  static void stated(uv_fs_t* request) noexcept {
    auto* self = static_cast<opening*>(request->data);
    auto guard = self->lifetime;
    const auto code = request->result;
    const auto info = request->statbuf;
    uv_fs_req_cleanup(request);
    if (code < 0 || (info.st_mode & S_IFMT) != S_IFREG) {
      self->fail(code < 0 ? from_uv_status(static_cast<int>(code))
                          : gneiss::engine::result::invalid_argument);
      return;
    }
    self->deliver(info.st_size);
  }
  void deliver(std::uint64_t size) noexcept {
    try {
      auto state = std::make_shared<file>();
      state->loop = loop;
      state->size = size;
      state->capacity = reserved.owner;
      // 在初始化 handle 前完成所有会失败的分配。
      auto source = std::make_shared<uv_async_file>(state);
      const auto code = uv_async_init(loop, &state->wakeup, file::dispatch);
      if (code < 0) {
        fail(from_uv_status(code));
        return;
      }
      state->wakeup.data = state.get();
      state->descriptor = std::exchange(descriptor, -1);
      auto* opened_file = state.get();
      opened_file->lifetime = std::move(state);
      output = std::move(source);
      publish(gneiss::engine::result::success);
      lifetime.reset();
    } catch (const std::bad_alloc&) {
      fail(gneiss::engine::result::out_of_memory);
    } catch (...) {
      fail(gneiss::engine::result::internal);
    }
  }
  static void opened(uv_fs_t* request) noexcept {
    auto* self = static_cast<opening*>(request->data);
    auto guard = self->lifetime;
    const auto code = request->result;
    uv_fs_req_cleanup(request);
    if (code < 0) {
      self->fail(from_uv_status(static_cast<int>(code)));
      return;
    }
    self->descriptor = static_cast<int>(code);
    request->data = self;
    const auto status = uv_fs_fstat(self->loop, request, self->descriptor, stated);
    if (status < 0) {
      uv_fs_req_cleanup(request);
      self->fail(from_uv_status(status));
    }
  }
};
} // namespace file_detail

uv_file_open_operation::uv_file_open_operation(std::shared_ptr<file_detail::opening> state) noexcept
    : state_(std::move(state)) {}
uv_file_open_operation::~uv_file_open_operation() = default;
gneiss::engine::result
uv_file_open_operation::poll(std::shared_ptr<uv_async_file>& output) noexcept {
  output.reset();
  if (delivered_) {
    return gneiss::engine::result::invalid_state;
  }
  if (!state_->ready.load(std::memory_order_acquire)) {
    return gneiss::engine::result::not_ready;
  }
  if (state_->status == gneiss::engine::result::success) {
    output = std::move(state_->output);
    delivered_ = true;
  }
  return state_->status;
}
uv_file_read_operation::uv_file_read_operation(std::shared_ptr<file_detail::reading> state) noexcept
    : state_(std::move(state)) {}
uv_file_read_operation::~uv_file_read_operation() = default;
gneiss::engine::result uv_file_read_operation::poll(std::span<const std::byte>& output) noexcept {
  output = {};
  if (!state_->ready.load(std::memory_order_acquire)) {
    return gneiss::engine::result::not_ready;
  }
  if (state_->status == gneiss::engine::result::success) {
    output = state_->bytes;
  }
  return state_->status;
}
gneiss::engine::result
uv_file_read_operation::wait_for(std::chrono::milliseconds timeout,
                                 std::span<const std::byte>& output) noexcept {
  output = {};
  if (timeout < std::chrono::milliseconds::zero()) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    std::unique_lock lock(state_->mutex);
    state_->changed.wait_for(lock, timeout,
                             [this] { return state_->ready.load(std::memory_order_acquire); });
    return poll(output);
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
uv_async_file::uv_async_file(std::shared_ptr<file_detail::file> state) noexcept
    : state_(std::move(state)) {}
uv_async_file::~uv_async_file() { begin_close(); }
void uv_async_file::begin_close() const noexcept {
  const std::scoped_lock lock(state_->mutex);
  if (state_->descriptor >= 0 && !state_->released) {
    state_->released = true;
    (void)uv_async_send(&state_->wakeup);
  }
}
gneiss::engine::result uv_async_file::poll_close() const noexcept {
  return state_->close_receipt.ready.load(std::memory_order_acquire)
             ? state_->close_receipt.status
             : gneiss::engine::result::not_ready;
}
std::uint64_t uv_async_file::size() const noexcept { return state_->size; }
gneiss::engine::result uv_async_file::read_at(std::uint64_t offset,
                                              std::span<std::byte> output) const noexcept {
  {
    const std::scoped_lock lock(state_->mutex);
    if (state_->released)
      return gneiss::engine::result::invalid_state;
    ++state_->active_reads;
  }
  // 关闭等待计数归零，不持锁等待同步系统调用，避免阻塞发起关闭的线程。
  struct finish_read {
    file_detail::file* state;
    ~finish_read() { state->read_finished(); }
  } finished{state_.get()};
  if (offset > size() || output.size() > size() - offset ||
      offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
      output.size() >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - offset) {
    return gneiss::engine::result::invalid_argument;
  }
  while (!output.empty()) {
    const auto count = static_cast<unsigned int>(
        std::min(output.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    auto buffer = uv_buf_init(reinterpret_cast<char*>(output.data()), count);
    uv_fs_t request{};
    const auto code = uv_fs_read(nullptr, &request, state_->descriptor, &buffer, 1U,
                                 static_cast<std::int64_t>(offset), nullptr);
    const auto consumed = request.result;
    uv_fs_req_cleanup(&request);
    if (code < 0) {
      return from_uv_status(code);
    }
    if (consumed <= 0 || std::cmp_greater(consumed, count)) {
      return gneiss::engine::result::io;
    }
    offset += static_cast<std::uint64_t>(consumed);
    output = output.subspan(static_cast<std::size_t>(consumed));
  }
  return gneiss::engine::result::success;
}
gneiss::engine::result
uv_async_file::begin_read(std::uint64_t offset, std::size_t count,
                          std::unique_ptr<uv_file_read_operation>& output) const noexcept {
  output.reset();
  if (count > state_->capacity->maximum_bytes || offset > size() || count > size() - offset ||
      offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
      count > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - offset) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    auto read = std::make_shared<file_detail::reading>();
    if (!read->reserved.acquire(state_->capacity, count)) {
      return gneiss::engine::result::not_ready;
    }
    read->bytes.resize(count);
    read->offset = offset;
    read->source = shared_from_this();
    auto operation = std::make_unique<uv_file_read_operation>(read);
    const std::scoped_lock lock(state_->mutex);
    if (state_->released)
      return gneiss::engine::result::invalid_state;
    state_->queued.push_back(read);
    const auto code = uv_async_send(&state_->wakeup);
    if (code < 0) {
      state_->queued.pop_back();
      return from_uv_status(code);
    }
    ++state_->active_reads;
    output = std::move(operation);
    return gneiss::engine::result::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
uv_file_io::uv_file_io(uv_loop_executor& executor, uv_file_limits limits)
    : executor_(executor), budget_(std::make_shared<file_detail::budget>(limits)) {}

gneiss::engine::result
uv_file_io::begin_open(std::string_view path,
                       std::unique_ptr<uv_file_open_operation>& output) const noexcept {
  output.reset();
  if (path.empty() || path.find('\0') != std::string_view::npos) {
    return gneiss::engine::result::invalid_argument;
  }
  try {
    auto pending = std::make_shared<file_detail::opening>();
    if (!pending->reserved.acquire(budget_, 0U)) {
      return gneiss::engine::result::not_ready;
    }
    pending->path = path;
    auto operation = std::make_unique<uv_file_open_operation>(pending);
    const auto started = uv_loop_access::post(executor_, [pending](uv_loop_t* loop) {
      pending->loop = loop;
      pending->request.data = pending.get();
      pending->lifetime = pending;
      const auto code = uv_fs_open(loop, &pending->request, pending->path.c_str(), O_RDONLY, 0,
                                   file_detail::opening::opened);
      if (code < 0) {
        uv_fs_req_cleanup(&pending->request);
        pending->fail(from_uv_status(code));
      }
    });
    if (started == gneiss::engine::result::success) {
      output = std::move(operation);
    }
    return started;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::result::out_of_memory;
  } catch (...) {
    return gneiss::engine::result::internal;
  }
}
} // namespace gneiss::engine
