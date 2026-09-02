// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "uv_runtime.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

namespace {

constexpr std::size_t producer_count = 8U;
constexpr std::size_t tasks_per_producer = 1000U;

bool run_multi_producer_test() {
  gneiss::uv_runtime runtime(64U);
  if (runtime.start() != gneiss::result::success) {
    return false;
  }

  std::atomic_size_t executed = 0U;
  std::atomic_bool failed = false;
  std::array<std::thread, producer_count> producers;
  for (auto& producer : producers) {
    producer = std::thread([&runtime, &executed, &failed] {
      for (std::size_t index = 0U; index < tasks_per_producer; ++index) {
        while (true) {
          const auto operation =
              runtime.post([&executed] { executed.fetch_add(1U, std::memory_order_relaxed); });
          if (operation == gneiss::result::success) {
            break;
          }
          if (operation != gneiss::result::not_ready || !runtime.is_running()) {
            failed.store(true, std::memory_order_relaxed);
            return;
          }
          std::this_thread::yield();
        }
      }
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  return runtime.stop() == gneiss::result::success && !failed.load(std::memory_order_relaxed) &&
         executed.load(std::memory_order_relaxed) == producer_count * tasks_per_producer;
}

bool run_concurrent_stop_test() {
  gneiss::uv_runtime runtime(32U);
  if (runtime.start() != gneiss::result::success) {
    return false;
  }

  std::atomic_bool stop_requested = false;
  std::atomic_bool failed = false;
  std::atomic_size_t accepted = 0U;
  std::atomic_size_t executed = 0U;
  std::array<std::thread, producer_count> producers;
  for (auto& producer : producers) {
    producer = std::thread([&] {
      while (!stop_requested.load(std::memory_order_acquire)) {
        const auto operation =
            runtime.post([&executed] { executed.fetch_add(1U, std::memory_order_relaxed); });
        if (operation == gneiss::result::success) {
          accepted.fetch_add(1U, std::memory_order_relaxed);
        } else if (operation == gneiss::result::not_ready) {
          std::this_thread::yield();
        } else {
          failed.store(true, std::memory_order_relaxed);
          return;
        }
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stop_requested.store(true, std::memory_order_release);
  const auto stopped = runtime.stop();
  for (auto& producer : producers) {
    producer.join();
  }
  return stopped == gneiss::result::success && !failed.load(std::memory_order_relaxed) &&
         accepted.load(std::memory_order_relaxed) == executed.load(std::memory_order_relaxed);
}

bool run_destructor_drain_test() {
  std::atomic_size_t executed = 0U;
  {
    gneiss::uv_runtime runtime(32U);
    if (runtime.start() != gneiss::result::success) {
      return false;
    }
    for (std::size_t index = 0U; index < 32U; ++index) {
      if (runtime.post([&executed] { executed.fetch_add(1U, std::memory_order_relaxed); }) !=
          gneiss::result::success) {
        return false;
      }
    }
  }
  return executed.load(std::memory_order_relaxed) == 32U;
}

} // namespace

int main() {
  return run_multi_producer_test() && run_concurrent_stop_test() && run_destructor_drain_test() ? 0
                                                                                                : 1;
}
