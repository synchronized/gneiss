// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_runtime.h"

#include <uv.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

namespace gneiss {

struct uv_runtime::implementation final {
  explicit implementation(std::size_t capacity) : queue_capacity(capacity) {}

  uv_loop_t loop{};
  uv_async_t wakeup{};
  std::thread worker;
  std::mutex mutex;
  std::condition_variable initialized;
  std::deque<task> tasks;
  std::size_t queue_capacity;
  std::atomic_bool running = false;
  bool initialization_finished = false;
  result initialization_result = result::not_ready;
  bool is_accepting = false;
  bool is_stopping = false;

  static void on_wakeup(uv_async_t* handle) noexcept {
    auto* self = static_cast<implementation*>(handle->data);
    std::deque<task> pending;
    bool should_stop = false;
    {
      const std::scoped_lock lock(self->mutex);
      pending.swap(self->tasks);
      should_stop = self->is_stopping;
    }
    for (auto& operation : pending) {
      try {
        operation();
      } catch (...) {
        // 内部任务不得让异常越过 libuv 的 C 回调边界。
      }
    }
    if (should_stop && uv_is_closing(reinterpret_cast<uv_handle_t*>(handle)) == 0) {
      uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    }
  }

  void run() noexcept {
    const auto loop_result = uv_loop_init(&loop);
    auto operation = loop_result == 0 ? result::success : result::initialization_failed;
    if (operation == result::success) {
      wakeup.data = this;
      if (uv_async_init(&loop, &wakeup, on_wakeup) != 0) {
        operation = result::initialization_failed;
        (void)uv_loop_close(&loop);
      }
    }
    {
      const std::scoped_lock lock(mutex);
      initialization_result = operation;
      initialization_finished = true;
      is_accepting = operation == result::success;
      running.store(is_accepting, std::memory_order_release);
    }
    initialized.notify_one();
    if (operation != result::success) {
      return;
    }

    (void)uv_run(&loop, UV_RUN_DEFAULT);
    (void)uv_loop_close(&loop);
    running.store(false, std::memory_order_release);
  }
};

uv_runtime::uv_runtime(std::size_t queue_capacity)
    : implementation_(std::make_unique<implementation>(queue_capacity)) {}

uv_runtime::~uv_runtime() {
  if (implementation_) {
    (void)stop();
  }
}

result uv_runtime::start() noexcept {
  if (!implementation_ || implementation_->queue_capacity == 0U) {
    return result::invalid_argument;
  }
  if (implementation_->worker.joinable()) {
    return result::invalid_state;
  }
  try {
    {
      const std::scoped_lock lock(implementation_->mutex);
      implementation_->initialization_finished = false;
      implementation_->initialization_result = result::not_ready;
      implementation_->is_accepting = false;
      implementation_->is_stopping = false;
      implementation_->tasks.clear();
    }
    implementation_->worker = std::thread([state = implementation_.get()] { state->run(); });
    std::unique_lock lock(implementation_->mutex);
    implementation_->initialized.wait(lock,
                                      [this] { return implementation_->initialization_finished; });
    const auto operation = implementation_->initialization_result;
    lock.unlock();
    if (operation != result::success && implementation_->worker.joinable()) {
      implementation_->worker.join();
    }
    return operation;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::initialization_failed;
  }
}

result uv_runtime::post(task operation) noexcept {
  if (!implementation_ || !operation) {
    return result::invalid_argument;
  }
  try {
    {
      const std::scoped_lock lock(implementation_->mutex);
      if (!implementation_->is_accepting || implementation_->is_stopping) {
        return result::not_ready;
      }
      if (implementation_->tasks.size() >= implementation_->queue_capacity) {
        return result::not_ready;
      }
      implementation_->tasks.push_back(std::move(operation));
    }
    return uv_async_send(&implementation_->wakeup) == 0 ? result::success : result::io;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  } catch (...) {
    return result::internal;
  }
}

result uv_runtime::stop() noexcept {
  if (!implementation_ || !implementation_->worker.joinable()) {
    return result::not_ready;
  }
  if (std::this_thread::get_id() == implementation_->worker.get_id()) {
    return result::invalid_state;
  }
  {
    const std::scoped_lock lock(implementation_->mutex);
    implementation_->is_accepting = false;
    implementation_->is_stopping = true;
  }
  const auto wakeup_result = uv_async_send(&implementation_->wakeup);
  implementation_->worker.join();
  return wakeup_result == 0 ? result::success : result::io;
}

bool uv_runtime::is_running() const noexcept {
  return implementation_ && implementation_->running.load(std::memory_order_acquire);
}

} // namespace gneiss
