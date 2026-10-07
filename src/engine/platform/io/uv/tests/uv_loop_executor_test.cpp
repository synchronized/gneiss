// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_loop_executor.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#ifndef _WIN32
#include <array>
#include <cerrno>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
bool broken_pipe_on_worker() {
  // 子进程恢复默认处置，避免测试启动器继承的 SIG_IGN 掩盖进程终止缺陷。
  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  sigset_t signal{};
  sigemptyset(&signal);
  sigaddset(&signal, SIGPIPE);
  if (sigaction(SIGPIPE, &action, nullptr) != 0 ||
      pthread_sigmask(SIG_UNBLOCK, &signal, nullptr) != 0) {
    return false;
  }
  gneiss::io_internal::uv_loop_executor executor;
  for (int attempt = 0; attempt < 2; ++attempt) {
    std::array<int, 2> descriptors{};
    if (pipe(descriptors.data()) != 0) {
      return false;
    }
    close(descriptors[0]);
    std::promise<bool> observed;
    auto future = observed.get_future();
    const auto started = executor.start();
    const auto posted = executor.post([&observed, fd = descriptors[1]] {
      const char byte = 'x';
      const auto count = write(fd, &byte, 1U);
      const auto error = errno;
      sigset_t pending{};
      const bool has_pending = sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;
      observed.set_value(count == -1 && error == EPIPE && has_pending);
    });
    const bool completed = future.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const auto stopped = executor.stop();
    close(descriptors[1]);
    if (started != gneiss::result::success || posted != gneiss::result::success || !completed ||
        stopped != gneiss::result::success || !future.get()) {
      return false;
    }
    sigset_t host_mask{};
    struct sigaction host_action{};
    if (pthread_sigmask(SIG_BLOCK, nullptr, &host_mask) != 0 ||
        sigismember(&host_mask, SIGPIPE) != 0 || sigaction(SIGPIPE, nullptr, &host_action) != 0 ||
        host_action.sa_handler != SIG_DFL) {
      return false;
    }
  }
  return true;
}
bool test_broken_pipe() {
  const auto child = fork();
  if (child == 0) {
    alarm(10U);
    _exit(broken_pipe_on_worker() ? 0 : 1);
  }
  if (child < 0) {
    return false;
  }
  int status{};
  return waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
} // namespace
#endif

int main() {
#ifndef _WIN32
  if (!test_broken_pipe()) {
    return 9;
  }
#endif
  gneiss::io_internal::uv_loop_executor invalid(0U);
  if (invalid.start() != gneiss::result::invalid_argument) {
    return 1;
  }

  gneiss::io_internal::uv_loop_executor runtime(2U);
  if (runtime.post([] {}) != gneiss::result::not_ready ||
      runtime.start() != gneiss::result::success || !runtime.is_running() ||
      runtime.start() != gneiss::result::invalid_state) {
    return 2;
  }

  std::promise<std::thread::id> executed;
  auto executed_future = executed.get_future();
  const auto owner_thread = std::this_thread::get_id();
  if (runtime.post([&executed] { executed.set_value(std::this_thread::get_id()); }) !=
          gneiss::result::success ||
      executed_future.wait_for(std::chrono::seconds(2)) != std::future_status::ready ||
      executed_future.get() == owner_thread) {
    return 3;
  }

  std::atomic_int count = 0;
  std::promise<void> entered;
  auto entered_future = entered.get_future();
  std::promise<void> release;
  auto gate = release.get_future().share();
  if (runtime.post([&count, &entered, gate] {
        entered.set_value();
        gate.wait();
        count.fetch_add(1, std::memory_order_relaxed);
      }) != gneiss::result::success ||
      entered_future.wait_for(std::chrono::seconds(2)) != std::future_status::ready ||
      runtime.post([&count] { count.fetch_add(1, std::memory_order_relaxed); }) !=
          gneiss::result::success ||
      runtime.post([&count] { count.fetch_add(1, std::memory_order_relaxed); }) !=
          gneiss::result::success ||
      runtime.post([] {}) != gneiss::result::not_ready) {
    return 4;
  }
  release.set_value();
  if (runtime.stop() != gneiss::result::success || runtime.is_running() ||
      count.load(std::memory_order_relaxed) != 3 ||
      runtime.post([] {}) != gneiss::result::not_ready ||
      runtime.stop() != gneiss::result::not_ready) {
    return 5;
  }

  if (runtime.start() != gneiss::result::success ||
      runtime.post([] { throw 1; }) != gneiss::result::success ||
      runtime.stop() != gneiss::result::success || runtime.failed_task_count() != 1U) {
    return 6;
  }

  if (runtime.start() != gneiss::result::success || runtime.failed_task_count() != 0U) {
    return 7;
  }
  std::promise<gneiss::result> self_stop;
  auto self_stop_future = self_stop.get_future();
  if (runtime.post([&runtime, &self_stop] { self_stop.set_value(runtime.stop()); }) !=
          gneiss::result::success ||
      self_stop_future.wait_for(std::chrono::seconds(2)) != std::future_status::ready ||
      self_stop_future.get() != gneiss::result::invalid_state ||
      runtime.stop() != gneiss::result::success) {
    return 8;
  }
  return 0;
}
