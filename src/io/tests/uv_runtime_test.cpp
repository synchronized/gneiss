// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_runtime.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

int main() {
  gneiss::uv_runtime invalid(0U);
  if (invalid.start() != gneiss::result::invalid_argument) {
    return 1;
  }

  gneiss::uv_runtime runtime(2U);
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
