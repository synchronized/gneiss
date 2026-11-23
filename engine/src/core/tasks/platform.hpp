// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <utility>

#if GNEISS_TASK_THREADS
#include <condition_variable>
#include <thread>
#include <vector>
#endif

namespace gneiss::tasks::detail {

#if GNEISS_TASK_THREADS
using mutex = std::mutex;
using condition = std::condition_variable;
class workers final {
public:
  template <class Function> void start(Function function) {
    threads_.emplace_back(std::move(function));
  }
  void join() {
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

private:
  std::vector<std::jthread> threads_;
};
#else
// 无线程产物只由宿主线程访问，不创建 pthread 或条件变量等待后端。
class mutex final {
public:
  void lock() noexcept {}
  void unlock() noexcept {}
};
class condition final {
public:
  void notify_all() noexcept {}
  template <class Lock> void wait(Lock&) { throw std::logic_error("无线程后端禁止阻塞等待"); }
  template <class Lock, class Predicate> void wait(Lock&, Predicate predicate) {
    if (!predicate()) {
      throw std::logic_error("无线程后端禁止阻塞等待");
    }
  }
  template <class Lock, class Time> void wait_until(Lock& lock, Time) { wait(lock); }
};
class workers final {
public:
  template <class Function> void start(Function) {
    throw std::invalid_argument("工作线程后端不可用");
  }
  void join() noexcept {}
};
#endif

} // namespace gneiss::tasks::detail
