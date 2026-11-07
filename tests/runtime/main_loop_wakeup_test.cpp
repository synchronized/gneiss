// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/tasks/task_scheduler.hpp"
#include "runtime/main_loop_wakeup.hpp"

#include <barrier>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
int main() try {
  auto signal = std::make_shared<gneiss::runtime::main_loop_wakeup>();
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
  using namespace gneiss::engine;
  task_scheduler scheduler({.mode = execution_mode::cooperative});
  const auto scope = scheduler.make_scope(signal);
  task_handle success;
  task_handle failure;
  task_handle cancelled;
  const auto before = signal->snapshot();
  const auto good = [](const task_context& context) {
    return task_outcome{
        .state = context.allows_blocking_wait() ? task_state::failed : task_state::succeeded,
    };
  };
  if (scheduler.submit({.name = "success", .scope = scope}, good, success) !=
          submit_result::success ||
      scheduler.submit(
          {.name = "failure", .scope = scope},
          [](const task_context&) -> task_outcome { throw std::runtime_error("测试"); },
          failure) != submit_result::success ||
      scheduler.submit({.name = "cancel", .scope = scope}, good, cancelled) !=
          submit_result::success ||
      !scheduler.cancel(cancelled)) {
    return 4;
  }
  if (scheduler.run_ready().executed != 2U || signal->snapshot() != before + 3U ||
      !signal->wait(before, 0ms)) {
    return 5;
  }
  auto other_signal = std::make_shared<gneiss::runtime::main_loop_wakeup>();
  const auto other_scope = scheduler.make_scope(other_signal);
  task_handle other_task;
  const auto own_generation = signal->snapshot();
  if (scheduler.submit({.scope = other_scope}, good, other_task) != submit_result::success ||
      scheduler.run_ready().executed != 1U || other_signal->snapshot() != 1U ||
      signal->snapshot() != own_generation || !scheduler.close_scope(other_scope))
    return 11;
  // 宿主引用释放后，scope 仍持有独立唤醒对象，不持有宿主本体。
  std::weak_ptr<gneiss::engine::wakeup_signal> weak = signal;
  signal.reset();
  if (weak.expired()) {
    return 6;
  }
  std::vector<task_completion> results;
  if (scheduler.poll(scope, results) != 3U || weak.expired() ||
      results[0].outcome.state != task_state::succeeded ||
      results[1].outcome.state != task_state::failed ||
      results[2].outcome.state != task_state::cancelled || !scheduler.close_scope(scope) ||
      !weak.expired()) {
    return 7;
  }
  task_scheduler workers({.workers = 1U});
  auto worker_signal = std::make_shared<gneiss::runtime::main_loop_wakeup>();
  const auto worker_scope = workers.make_scope(worker_signal);
  task_handle worker_task;
  if (workers.submit(
          {.scope = worker_scope},
          [](const task_context& context) {
            return task_outcome{
                .state =
                    context.allows_blocking_wait() ? task_state::succeeded : task_state::failed,
            };
          },
          worker_task) != submit_result::success ||
      !worker_signal->wait(0U, 2s)) {
    return 9;
  }
  results.clear();
  if (workers.poll(worker_scope, results) != 1U ||
      results.front().outcome.state != task_state::succeeded ||
      !workers.close_scope(worker_scope)) {
    return 10;
  }
  // 任务已运行时撤销宿主引用；关闭等待迟到完成，信号不得引用宿主内存。
  auto late_signal = std::make_shared<gneiss::runtime::main_loop_wakeup>();
  std::weak_ptr<wakeup_signal> late_weak = late_signal;
  const auto late_scope = workers.make_scope(late_signal);
  std::barrier late_barrier(2);
  task_handle late_task;
  if (workers.submit(
          {.scope = late_scope},
          [&](const task_context&) {
            late_barrier.arrive_and_wait();
            late_barrier.arrive_and_wait();
            return task_outcome{.state = task_state::succeeded};
          },
          late_task) != submit_result::success)
    return 12;
  late_barrier.arrive_and_wait();
  late_signal.reset();
  const bool retained = !late_weak.expired();
  late_barrier.arrive_and_wait();
  if (!workers.close_scope(late_scope) || !retained || !late_weak.expired())
    return 13;
  return 0;
} catch (...) {
  return 8;
}
