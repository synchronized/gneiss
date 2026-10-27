// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_path_resolver.hpp"
#include "engine/core/result.hpp"

#include "uv_loop_access.hpp"
#include "uv_result.hpp"

#include <new>
#include <utility>

namespace gneiss::engine {
struct uv_path_operation::state {
  uv_fs_t request{};
  std::string path;
  std::string resolved;
  std::shared_ptr<std::atomic_size_t> active;
  std::shared_ptr<state> in_flight;
  std::atomic_bool ready = false;
  result status = result::not_ready;
  bool reserved{};

  ~state() { release(); }
  void release() noexcept {
    if (std::exchange(reserved, false)) {
      active->fetch_sub(1U, std::memory_order_relaxed);
    }
  }
  void finish(int code) noexcept {
    status = from_uv_status(code);
    if (code == 0) {
      try {
        if (request.ptr == nullptr) {
          status = result::internal;
        } else {
          resolved = static_cast<const char*>(request.ptr);
        }
      } catch (const std::bad_alloc&) {
        status = result::out_of_memory;
      } catch (...) {
        status = result::internal;
      }
    }
    uv_fs_req_cleanup(&request);
    release();
    ready.store(true, std::memory_order_release);
  }
  static void completed(uv_fs_t* request) noexcept {
    auto* pending = static_cast<state*>(request->data);
    // 回调保留自身直到清理完成；消费者可在任意时刻放弃结果。
    auto lifetime = std::move(pending->in_flight);
    pending->finish(static_cast<int>(request->result));
  }
};

uv_path_operation::uv_path_operation(std::shared_ptr<state> pending) noexcept
    : pending_(std::move(pending)) {}
uv_path_operation::~uv_path_operation() = default;

result uv_path_operation::poll(std::string& output) noexcept {
  output.clear();
  if (delivered_) {
    return result::invalid_state;
  }
  if (!pending_->ready.load(std::memory_order_acquire)) {
    return result::not_ready;
  }
  if (pending_->status == result::success) {
    output = std::move(pending_->resolved);
    delivered_ = true;
  }
  return pending_->status;
}

uv_path_resolver::uv_path_resolver(uv_loop_executor& executor, std::size_t capacity)
    : executor_(executor), active_(std::make_shared<std::atomic_size_t>(0U)), capacity_(capacity) {}

result uv_path_resolver::begin(std::string_view path,
                               std::unique_ptr<uv_path_operation>& output) const noexcept {
  output.reset();
  if (path.empty() || path.find('\0') != std::string_view::npos || capacity_ == 0U) {
    return result::invalid_argument;
  }
  try {
    auto pending = std::make_shared<uv_path_operation::state>();
    pending->active = active_;
    auto active = active_->load(std::memory_order_relaxed);
    do {
      if (active >= capacity_) {
        return result::not_ready;
      }
    } while (!active_->compare_exchange_weak(active, active + 1U, std::memory_order_relaxed));
    pending->reserved = true;
    pending->path = path;
    auto operation = std::unique_ptr<uv_path_operation>(new uv_path_operation(pending));
    const auto submitted = uv_loop_access::post(executor_, [pending](uv_loop_t* loop) {
      pending->request.data = pending.get();
      pending->in_flight = pending;
      const auto code = uv_fs_realpath(loop, &pending->request, pending->path.c_str(),
                                       uv_path_operation::state::completed);
      if (code < 0) {
        pending->finish(code);
        pending->in_flight.reset();
      }
    });
    if (submitted == result::success) {
      output = std::move(operation);
    }
    return submitted;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}
} // namespace gneiss::engine
