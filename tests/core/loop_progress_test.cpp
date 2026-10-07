// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/loop_progress.hpp"
#include "engine/core/tasks/task_scheduler.hpp"

#include <barrier>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
int main() try {
  auto signal = std::make_shared<gneiss::core::loop_progress>();
  const auto initial = signal->snapshot();
  if (signal->wait(initial, 1ms)) {
    return 1;
  }
  signal->notify();
  signal->notify();
  if (!signal->wait(initial, 0ms) || signal->wait(signal->snapshot(), 0ms)) {
    return 2;
  }
  // 同步到检查/等待边界，允许通知发生在 wait 前或其中；两种情况都不得丢失。
  std::barrier barrier(2);
  std::jthread producer([&] {
    for (unsigned index = 0U; index < 128U; ++index) {
      barrier.arrive_and_wait();
      signal->notify();
      barrier.arrive_and_wait();
    }
  });
  bool all_woken = true;
  for (unsigned index = 0U; index < 128U; ++index) {
    const auto observed = signal->snapshot();
    barrier.arrive_and_wait();
    all_woken = signal->wait(observed, 2s) && all_woken;
    barrier.arrive_and_wait();
  }
  producer.join();
  if (!all_woken) {
    return 3;
  }
  using namespace gneiss::tasks;
  task_scheduler scheduler({.mode = execution_mode::cooperative});
  const auto scope = scheduler.make_scope();
  task_handle success;
  task_handle failure;
  task_handle cancelled;
  const auto before = signal->snapshot();
  const auto good = [](const task_context&) { return task_outcome{}; };
  if (scheduler.submit({.name = "success", .scope = scope, .notification = signal}, good,
                       success) != submit_result::success ||
      scheduler.submit(
          {.name = "failure", .scope = scope, .notification = signal},
          [](const task_context&) -> task_outcome { throw std::runtime_error("测试"); },
          failure) != submit_result::success ||
      scheduler.submit({.name = "cancel", .scope = scope, .notification = signal}, good,
                       cancelled) != submit_result::success ||
      !scheduler.cancel(cancelled)) {
    return 4;
  }
  if (scheduler.run_ready().executed != 2U || signal->snapshot() != before + 3U ||
      !signal->wait(before, 0ms)) {
    return 5;
  }
  // 宿主引用释放后，未消费回执仍持有独立通知对象，不持有宿主本体。
  std::weak_ptr<gneiss::core::progress_notification> weak = signal;
  signal.reset();
  if (weak.expired()) {
    return 6;
  }
  std::vector<task_completion> results;
  if (scheduler.poll(scope, results) != 3U || !weak.expired() ||
      results[0].outcome.state != task_state::succeeded ||
      results[1].outcome.state != task_state::failed ||
      results[2].outcome.state != task_state::cancelled || !scheduler.close_scope(scope)) {
    return 7;
  }
  return 0;
} catch (...) {
  return 8;
}
