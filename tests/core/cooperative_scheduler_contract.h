// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/tasks/task_scheduler.hpp"

#include <stdexcept>

// 桌面与浏览器复用同一组无阻塞契约；不使用 sleep 或工作线程屏障。
inline void cooperative_scheduler_contract() {
  using namespace gneiss;
  using namespace std::chrono_literals;
  const auto check = [](bool value) {
    if (!value) {
      throw std::runtime_error("协作调度契约失败");
    }
  };
  auto now = std::chrono::steady_clock::time_point{} + 1s;
  gneiss::tasks::task_scheduler scheduler(
      {.capacity = 8U, .mode = gneiss::tasks::execution_mode::cooperative, .clock = [&] { return now; }});
  const auto scope = scheduler.make_scope();
  const auto lane = scheduler.make_serial_queue(scope);
  unsigned count{};
  gneiss::tasks::task_handle first;
  check(scheduler.submit(
            {.scope = scope, .serial = lane},
            [&](const auto&) {
              check(scheduler.run_ready().status == gneiss::tasks::drive_status::reentrant);
              check(!scheduler.try_close_scope(scope));
              check(!scheduler.close_scope(scope));
              ++count;
              now += 3ms;
              return gneiss::tasks::task_outcome{};
            },
            first) == gneiss::tasks::submit_result::success);
  gneiss::tasks::task_handle second;
  check(scheduler.submit(
            {.scope = scope, .serial = lane, .prerequisites = {first}},
            [&](const auto&) {
              ++count;
              return gneiss::tasks::task_outcome{};
            },
            second) == gneiss::tasks::submit_result::success);
  gneiss::tasks::task_completion completion;
  std::vector<gneiss::tasks::task_completion> completed;
  check(scheduler.poll(scope, completed) == 0U && scheduler.query(first, completion));
  check(count == 0U && scheduler.run_ready({.max_tasks = 0U}).executed == 0U);
  check(scheduler.run_ready({.max_time = 0ns}).executed == 0U);
  const auto drive = scheduler.run_ready({.max_tasks = 8U, .max_time = 2ms});
  check(drive.executed == 1U && drive.budget_exhausted && count == 1U);
  check(scheduler.run_ready({.max_tasks = 1U}).executed == 1U && count == 2U);
  check(scheduler.poll(scope, completed) == 2U && !scheduler.query(first, completion));

  gneiss::tasks::task_handle delayed;
  check(scheduler.submit(
            {.scope = scope, .not_before = now + 1s},
            [&](const auto&) {
              ++count;
              return gneiss::tasks::task_outcome{};
            },
            delayed) == gneiss::tasks::submit_result::success);
  check(scheduler.run_ready().executed == 0U && count == 2U);
  now += 1s;
  check(scheduler.run_ready().executed == 1U && count == 3U);
  gneiss::tasks::task_handle failed;
  check(scheduler.submit(
            {.scope = scope},
            [](const auto&) -> gneiss::tasks::task_outcome {
              return {.state = gneiss::tasks::task_state::failed, .error = "预期失败"};
            },
            failed) == gneiss::tasks::submit_result::success);
  gneiss::tasks::task_handle dependent;
  check(scheduler.submit(
            {.scope = scope, .prerequisites = {failed}},
            [&](const auto&) {
              ++count;
              return gneiss::tasks::task_outcome{};
            },
            dependent) == gneiss::tasks::submit_result::success);
  check(scheduler.run_ready().executed == 1U && count == 3U);
  check(scheduler.query(dependent, completion) &&
        completion.outcome.state == gneiss::tasks::task_state::dependency_failed);
  gneiss::tasks::task_handle cancelled;
  check(scheduler.submit(
            {.scope = scope, .not_before = now + 24h},
            [&](const auto&) {
              ++count;
              return gneiss::tasks::task_outcome{};
            },
            cancelled) == gneiss::tasks::submit_result::success);
  scheduler.request_stop();
  check(scheduler.stopped() && scheduler.run_ready().status == gneiss::tasks::drive_status::stopped);
  check(scheduler.query(cancelled, completion) &&
        completion.outcome.state == gneiss::tasks::task_state::cancelled);
  check(scheduler.try_close_scope(scope) && scheduler.stats().retained == 0U && count == 3U);

  gneiss::tasks::task_scheduler bounded({.capacity = 2U, .mode = gneiss::tasks::execution_mode::cooperative});
  const auto left = bounded.make_scope();
  const auto right = bounded.make_scope();
  gneiss::tasks::task_handle left_task;
  gneiss::tasks::task_handle right_task;
  const auto body = [](const auto&) { return gneiss::tasks::task_outcome{}; };
  check(bounded.submit({.scope = left}, body, left_task) == gneiss::tasks::submit_result::success);
  check(bounded.submit({.scope = right}, body, right_task) == gneiss::tasks::submit_result::success);
  gneiss::tasks::task_handle overflow;
  check(bounded.submit({.scope = right}, body, overflow) == gneiss::tasks::submit_result::full);
  bounded.cancel_scope(left);
  check(bounded.run_ready().executed == 1U);
  check(bounded.query(right_task, completion) && completion.outcome.state == gneiss::tasks::task_state::succeeded);
  // 未消费的终态仍占容量，取消一个作用域不影响另一个。
  check(bounded.submit({.scope = right}, body, overflow) == gneiss::tasks::submit_result::full);
  check(bounded.try_close_scope(left));
  check(bounded.submit({.scope = right}, body, overflow) == gneiss::tasks::submit_result::success);

  gneiss::tasks::task_scheduler fair({.capacity = 16U, .mode = gneiss::tasks::execution_mode::cooperative});
  const auto fair_scope = fair.make_scope();
  unsigned normals{};
  unsigned observed = 99U;
  for (unsigned i = 0; i < 12U; ++i) {
    check(fair.submit(
              {.scope = fair_scope, .priority = gneiss::tasks::task_priority::normal},
              [&](const auto&) {
                ++normals;
                return gneiss::tasks::task_outcome{};
              },
              overflow) == gneiss::tasks::submit_result::success);
  }
  check(fair.submit(
            {.scope = fair_scope},
            [&](const auto&) {
              observed = normals;
              return gneiss::tasks::task_outcome{};
            },
            overflow) == gneiss::tasks::submit_result::success);
  check(fair.run_ready({.max_tasks = 16U, .max_time = 1s}).executed == 13U && observed == 8U);
}
