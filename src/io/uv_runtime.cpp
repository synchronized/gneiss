// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_runtime.h"

#include "uv_error.h"

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
  std::atomic_size_t failed_tasks = 0U;
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
        self->failed_tasks.fetch_add(1U, std::memory_order_relaxed);
      }
    }
    if (should_stop && uv_is_closing(reinterpret_cast<uv_handle_t*>(handle)) == 0) {
      uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    }
  }

  void run() noexcept {
    auto operation = from_uv_error(uv_loop_init(&loop));
    if (operation == result::success) {
      wakeup.data = this;
      operation = from_uv_error(uv_async_init(&loop, &wakeup, on_wakeup));
      if (operation != result::success) {
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
      implementation_->failed_tasks.store(0U, std::memory_order_relaxed);
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
    const std::scoped_lock lock(implementation_->mutex);
    if (!implementation_->is_accepting || implementation_->is_stopping) {
      return result::not_ready;
    }
    if (implementation_->tasks.size() >= implementation_->queue_capacity) {
      return result::not_ready;
    }
    implementation_->tasks.push_back(std::move(operation));
    const auto wakeup_result = from_uv_error(uv_async_send(&implementation_->wakeup));
    if (wakeup_result != result::success) {
      implementation_->tasks.pop_back();
    }
    return wakeup_result;
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
  int wakeup_result = 0;
  {
    const std::scoped_lock lock(implementation_->mutex);
    implementation_->is_accepting = false;
    implementation_->is_stopping = true;
    wakeup_result = uv_async_send(&implementation_->wakeup);
  }
  implementation_->worker.join();
  return from_uv_error(wakeup_result);
}

bool uv_runtime::is_running() const noexcept {
  return implementation_ && implementation_->running.load(std::memory_order_acquire);
}

std::size_t uv_runtime::failed_task_count() const noexcept {
  return implementation_ ? implementation_->failed_tasks.load(std::memory_order_relaxed) : 0U;
}

} // namespace gneiss
