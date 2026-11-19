// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/platform/io/executor.hpp>

#include "detail/uv_error.hpp"
#include "detail/uv_loop_access.hpp"

#include <uv.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

namespace gneiss::io {

struct executor::implementation final {
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
  error initialization_result = error::not_ready;
  bool is_accepting = false;
  bool is_stopping = false;

  static void on_wakeup(uv_async_t* handle) try {
    auto* self = static_cast<implementation*>(handle->data);
    std::deque<task> pending;
    bool should_stop = false;
    {
      const std::scoped_lock lock(self->mutex);
      pending.swap(self->tasks);
      should_stop = self->is_stopping;
    }
    for (auto& operation : pending) {
      {
        operation();
      }
    }
    if (should_stop && uv_is_closing(reinterpret_cast<uv_handle_t*>(handle)) == 0) {
      uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
    }
  } catch (...) {
    gneiss::fatal_current_exception();
  }

  void run() noexcept try {
    auto operation = error::success;
#ifndef _WIN32
    // libuv 的 POSIX 流写入可能产生 SIGPIPE。只在自有 I/O 线程阻塞它，
    // 保留 EPIPE 回执，不改变宿主的进程级信号处理器或调用线程掩码。
    // 线程退出时丢弃其待决信号，不在退出前解除阻塞。
    sigset_t blocked{};
    if (sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGPIPE) != 0 ||
        pthread_sigmask(SIG_BLOCK, &blocked, nullptr) != 0) {
      operation = error::initialization_failed;
    }
#endif
    if (operation == error::success) {
      operation = detail::from_uv_status(uv_loop_init(&loop));
    }
    if (operation == error::success) {
      wakeup.data = this;
      operation = detail::from_uv_status(uv_async_init(&loop, &wakeup, on_wakeup));
      if (operation != error::success) {
        (void)uv_loop_close(&loop);
      }
    }
    {
      const std::scoped_lock lock(mutex);
      initialization_result = operation;
      initialization_finished = true;
      is_accepting = operation == error::success;
      running.store(is_accepting, std::memory_order_release);
    }
    initialized.notify_one();
    if (operation != error::success) {
      return;
    }

    (void)uv_run(&loop, UV_RUN_DEFAULT);
    (void)uv_loop_close(&loop);
    running.store(false, std::memory_order_release);
  } catch (...) {
    gneiss::fatal_current_exception();
  }
};

executor::executor(std::size_t queue_capacity)
    : implementation_(std::make_unique<implementation>(queue_capacity)) {}

executor::~executor() {
  if (implementation_) {
    (void)stop();
  }
}

error executor::start() {
  if (!implementation_ || implementation_->queue_capacity == 0U) {
    return error::invalid_argument;
  }
  if (implementation_->worker.joinable()) {
    return error::invalid_state;
  }
  {
    {
      const std::scoped_lock lock(implementation_->mutex);
      implementation_->initialization_finished = false;
      implementation_->initialization_result = error::not_ready;
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
    if (operation != error::success && implementation_->worker.joinable()) {
      implementation_->worker.join();
    }
    return operation;
  }
}

error executor::post(task operation) {
  if (!implementation_ || !operation) {
    return error::invalid_argument;
  }
  {
    const std::scoped_lock lock(implementation_->mutex);
    if (!implementation_->is_accepting || implementation_->is_stopping) {
      return error::not_ready;
    }
    if (implementation_->tasks.size() >= implementation_->queue_capacity) {
      return error::not_ready;
    }
    implementation_->tasks.push_back(std::move(operation));
    const auto wakeup_result = detail::from_uv_status(uv_async_send(&implementation_->wakeup));
    if (wakeup_result != error::success) {
      implementation_->tasks.pop_back();
    }
    return wakeup_result;
  }
}

error executor::stop() noexcept {
  if (!implementation_ || !implementation_->worker.joinable()) {
    return error::not_ready;
  }
  if (std::this_thread::get_id() == implementation_->worker.get_id()) {
    return error::invalid_state;
  }
  int wakeup_result = 0;
  {
    const std::scoped_lock lock(implementation_->mutex);
    implementation_->is_accepting = false;
    implementation_->is_stopping = true;
    wakeup_result = uv_async_send(&implementation_->wakeup);
  }
  implementation_->worker.join();
  return detail::from_uv_status(wakeup_result);
}

bool executor::is_running() const noexcept {
  return implementation_ && implementation_->running.load(std::memory_order_acquire);
}

error detail::uv_loop_access::post(executor& executor, task operation) {
  if (!operation || !executor.implementation_) {
    return error::invalid_argument;
  }
  {
    return executor.post([state = executor.implementation_.get(),
                          operation = std::move(operation)] { operation(&state->loop); });
  }
}

} // namespace gneiss::io
