// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "cooperative_scheduler_contract.h"
#include "engine/core/tasks/task_scheduler.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss;
using namespace std::chrono_literals;
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
template <class Predicate> void await(Predicate predicate) {
  const auto limit = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < limit, "等待任务超时");
    std::this_thread::yield();
  }
}
struct gate {
  std::promise<void> signal;
  std::shared_future<void> future{signal.get_future().share()};
  std::atomic_bool entered{};
  std::atomic_bool opened{};
  void open() {
    if (!opened.exchange(true)) {
      signal.set_value();
    }
  }
};
struct release_gate {
  std::shared_ptr<gate> value{std::make_shared<gate>()};
  ~release_gate() { value->open(); }
};
gneiss::tasks::task_handle launch(gneiss::tasks::task_scheduler& scheduler, gneiss::tasks::task_description description,
                   gneiss::tasks::task_scheduler::task_function body) {
  gneiss::tasks::task_handle handle;
  require(scheduler.submit(std::move(description), std::move(body), handle) ==
              gneiss::tasks::submit_result::success,
          "任务提交失败");
  return handle;
}
void dependency_graph(std::size_t workers) {
  std::atomic_int sum{};
  gneiss::tasks::task_scheduler scheduler({.workers = workers, .capacity = 32U});
  const auto scope = scheduler.make_scope();
  const auto a = launch(scheduler, {.name = "a", .scope = scope}, [&](const auto&) {
    sum = 1;
    return gneiss::tasks::task_outcome{};
  });
  const auto b =
      launch(scheduler, {.name = "b", .scope = scope, .prerequisites = {a}}, [&](const auto&) {
        ++sum;
        return gneiss::tasks::task_outcome{};
      });
  const auto c =
      launch(scheduler, {.name = "c", .scope = scope, .prerequisites = {a}}, [&](const auto&) {
        ++sum;
        return gneiss::tasks::task_outcome{};
      });
  const auto d =
      launch(scheduler, {.name = "d", .scope = scope, .prerequisites = {b, c}}, [&](const auto&) {
        require(sum == 3, "依赖提前运行");
        ++sum;
        return gneiss::tasks::task_outcome{};
      });
  await([&] { return scheduler.idle(scope); });
  gneiss::tasks::task_completion completed;
  require(scheduler.query(d, completed) && completed.outcome.state == gneiss::tasks::task_state::succeeded &&
              sum == 4,
          "依赖汇合失败");
  std::vector<gneiss::tasks::task_completion> results;
  require(scheduler.poll(scope, results, 2U) == 2U && scheduler.poll(scope, results) == 2U,
          "回执预算错误");
  require(scheduler.poll(scope, results) == 0U && !scheduler.query(a, completed),
          "回执重复或过期句柄有效");
  gneiss::tasks::task_handle rejected;
  require(scheduler.submit(
              {.scope = scope, .prerequisites = {a}}, [](const auto&) { return gneiss::tasks::task_outcome{}; },
              rejected) == gneiss::tasks::submit_result::invalid_argument,
          "已回收依赖未被拒绝");
  gneiss::tasks::task_scheduler other;
  require(other.submit(
              {.scope = other.make_scope(), .prerequisites = {d}},
              [](const auto&) { return gneiss::tasks::task_outcome{}; },
              rejected) == gneiss::tasks::submit_result::invalid_argument,
          "跨实例依赖未被拒绝");
  require(scheduler.close_scope(scope), "作用域关闭失败");
}
void dependencies_do_not_block() {
  std::atomic_bool independent{};
  gneiss::tasks::task_scheduler scheduler({.workers = 2U, .capacity = 16U});
  release_gate blocked;
  const auto scope = scheduler.make_scope();
  const auto first = launch(scheduler, {.scope = scope, .priority = gneiss::tasks::task_priority::normal},
                            [gate = blocked.value](const auto&) {
                              gate->entered = true;
                              gate->future.wait();
                              return gneiss::tasks::task_outcome{};
                            });
  await([&] { return blocked.value->entered.load(); });
  (void)launch(scheduler, {.scope = scope, .prerequisites = {first}},
               [](const auto&) { return gneiss::tasks::task_outcome{}; });
  (void)launch(scheduler, {.scope = scope, .priority = gneiss::tasks::task_priority::normal}, [&](const auto&) {
    independent = true;
    return gneiss::tasks::task_outcome{};
  });
  await([&] { return independent.load(); });
  blocked.value->open();
  require(scheduler.close_scope(scope), "关闭依赖队列失败");
}
void serial_failure_and_cancellation() {
  std::atomic_uint entered{};
  gneiss::tasks::task_scheduler scheduler({.workers = 4U, .capacity = 16U});
  release_gate blocked;
  const auto scope = scheduler.make_scope();
  const auto lane = scheduler.make_serial_queue(scope);
  const auto first = launch(
      scheduler, {.scope = scope, .serial = lane}, [gate = blocked.value](const auto& context) {
        gate->entered = true;
        gate->future.wait();
        return gneiss::tasks::task_outcome{.state = context.stop_requested() ? gneiss::tasks::task_state::cancelled
                                                              : gneiss::tasks::task_state::succeeded,
                            .error = {}};
      });
  await([&] { return blocked.value->entered.load(); });
  const auto dependent =
      launch(scheduler, {.scope = scope, .prerequisites = {first}}, [&](const auto&) {
        ++entered;
        return gneiss::tasks::task_outcome{};
      });
  const auto failed =
      launch(scheduler, {.scope = scope, .serial = lane}, [](const auto&) -> gneiss::tasks::task_outcome {
        return {.state = gneiss::tasks::task_state::failed, .error = "fixture"};
      });
  const auto last = launch(scheduler, {.scope = scope, .serial = lane}, [&](const auto&) {
    ++entered;
    return gneiss::tasks::task_outcome{};
  });
  require(scheduler.cancel(first), "运行任务不能请求取消");
  require(entered == 0U, "串行任务重叠");
  blocked.value->open();
  await([&] { return scheduler.idle(scope); });
  gneiss::tasks::task_completion status;
  require(scheduler.query(dependent, status) &&
              status.outcome.state == gneiss::tasks::task_state::dependency_failed,
          "取消未传播依赖");
  require(scheduler.query(failed, status) && status.outcome.state == gneiss::tasks::task_state::failed,
          "显式任务失败未保留");
  require(scheduler.query(last, status) && status.outcome.state == gneiss::tasks::task_state::succeeded &&
              entered == 1U,
          "串行失败阻止后续独立任务");
  require(!scheduler.cancel(last), "终态可重复取消");
}
void capacity_and_shutdown() {
  gneiss::tasks::task_scheduler scheduler({.workers = 1U, .capacity = 2U});
  const auto scope = scheduler.make_scope();
  const auto delayed =
      launch(scheduler, {.scope = scope, .not_before = std::chrono::steady_clock::now() + 1h},
             [](const auto&) { return gneiss::tasks::task_outcome{}; });
  const auto ready =
      launch(scheduler, {.scope = scope}, [](const auto&) { return gneiss::tasks::task_outcome{}; });
  gneiss::tasks::task_completion status;
  await([&] {
    return scheduler.query(ready, status) && status.outcome.state == gneiss::tasks::task_state::succeeded;
  });
  gneiss::tasks::task_handle rejected;
  require(scheduler.submit(
              {.scope = scope}, [](const auto&) { return gneiss::tasks::task_outcome{}; }, rejected) ==
              gneiss::tasks::submit_result::full,
          "未消费结果不计容量");
  require(scheduler.cancel(delayed), "延迟任务取消失败");
  std::vector<gneiss::tasks::task_completion> results;
  require(scheduler.poll(scope, results) == 2U, "取消回执丢失");
  require(scheduler.stats().rejected == 1U && scheduler.stats().retained == 0U, "调度统计错误");
  (void)launch(scheduler, {.scope = scope}, [&](const auto&) {
    require(!scheduler.close_scope(scope), "池内自等待未被拒绝");
    return gneiss::tasks::task_outcome{};
  });
  await([&] { return scheduler.idle(scope); });
  scheduler.stop();
  require(scheduler.submit(
              {.scope = scope}, [](const auto&) { return gneiss::tasks::task_outcome{}; }, rejected) ==
              gneiss::tasks::submit_result::stopped,
          "停止后接受任务");
}
void cancelled_serial_middle() {
  gneiss::tasks::task_scheduler scheduler({.workers = 3U, .capacity = 16U});
  release_gate blocked;
  const auto scope = scheduler.make_scope();
  const auto lane = scheduler.make_serial_queue(scope);
  const auto first =
      launch(scheduler, {.scope = scope, .serial = lane}, [gate = blocked.value](const auto&) {
        gate->entered = true;
        gate->future.wait();
        return gneiss::tasks::task_outcome{};
      });
  await([&] { return blocked.value->entered.load(); });
  const auto middle = launch(scheduler, {.scope = scope, .serial = lane},
                             [](const auto&) { return gneiss::tasks::task_outcome{}; });
  std::atomic_bool overlap{};
  const auto last = launch(scheduler, {.scope = scope, .serial = lane}, [&](const auto&) {
    gneiss::tasks::task_completion before;
    (void)scheduler.query(first, before);
    overlap = before.outcome.state != gneiss::tasks::task_state::succeeded;
    return gneiss::tasks::task_outcome{};
  });
  require(scheduler.cancel(middle), "串行中间项取消失败");
  // 独立任务证明另一工作槽已推进；后项仍须等待最早的运行任务。
  std::atomic_bool independent{};
  (void)launch(scheduler, {.scope = scope}, [&](const auto&) {
    independent = true;
    return gneiss::tasks::task_outcome{};
  });
  await([&] { return independent.load(); });
  gneiss::tasks::task_completion completion;
  require(scheduler.query(last, completion) && completion.outcome.state == gneiss::tasks::task_state::waiting,
          "取消中间项破坏串行顺序");
  blocked.value->open();
  await([&] { return scheduler.idle(scope); });
  require(!overlap.load(), "串行队列出现重叠");
}
void fair_priority_and_scope_isolation() {
  std::atomic_uint count{};
  std::atomic_uint background_order{99U};
  gneiss::tasks::task_scheduler scheduler({.workers = 1U, .capacity = 32U});
  release_gate blocked;
  const auto scope = scheduler.make_scope();
  const auto other = scheduler.make_scope();
  (void)launch(scheduler, {.scope = scope, .priority = gneiss::tasks::task_priority::normal},
               [gate = blocked.value](const auto&) {
                 gate->entered = true;
                 gate->future.wait();
                 return gneiss::tasks::task_outcome{};
               });
  await([&] { return blocked.value->entered.load(); });
  for (unsigned i = 0; i < 12U; ++i) {
    (void)launch(scheduler, {.scope = other, .priority = gneiss::tasks::task_priority::normal}, [&](const auto&) {
      ++count;
      return gneiss::tasks::task_outcome{};
    });
  }
  (void)launch(scheduler, {.scope = other}, [&](const auto&) {
    background_order = count.load();
    return gneiss::tasks::task_outcome{};
  });
  scheduler.cancel_scope(scope);
  blocked.value->open();
  require(scheduler.close_scope(scope), "关闭单独作用域失败");
  await([&] { return scheduler.idle(other); });
  require(background_order < 12U && count == 12U, "后台饥饿或关闭影响其他作用域");
}
void completion_queue_order() {
  gneiss::tasks::task_scheduler scheduler({.mode = gneiss::tasks::execution_mode::cooperative});
  const auto scope = scheduler.make_scope();
  const auto other = scheduler.make_scope();
  const auto first =
      launch(scheduler, {.scope = scope}, [](const auto&) { return gneiss::tasks::task_outcome{}; });
  const auto second =
      launch(scheduler, {.scope = scope}, [](const auto&) { return gneiss::tasks::task_outcome{}; });
  const auto foreign =
      launch(scheduler, {.scope = other}, [](const auto&) { return gneiss::tasks::task_outcome{}; });
  // 完成顺序与提交顺序不同；取消也只产生一次终态。
  require(scheduler.cancel(second), "取消第二个任务失败");
  require(scheduler.cancel(first), "取消第一个任务失败");
  (void)scheduler.cancel(second);
  (void)scheduler.run_ready();
  std::vector<gneiss::tasks::task_completion> output;
  require(scheduler.poll(scope, output, 0) == 0, "零预算消费了回执");
  require(scheduler.poll(scope, output, 1) == 1 && output.front().task.id == second.id,
          "完成队列没有保持终态到达顺序");
  require(scheduler.poll(scope, output, 1) == 1 && output.back().task.id == first.id,
          "完成队列遗漏终态");
  require(scheduler.poll(scope, output) == 0, "重复领取终态");
  require(scheduler.close_scope(scope), "关闭已消费 scope 失败");
  output.clear();
  require(scheduler.poll(other, output) == 1 && output.front().task.id == foreign.id,
          "关闭影响其他 scope 的完成队列");
}
void submission_observation() {
  gneiss::tasks::task_scheduler scheduler({.mode = gneiss::tasks::execution_mode::cooperative});
  gneiss::tasks::task_submission_timings timing{.allocate_ms = 100.0};
  gneiss::tasks::task_handle handle;
  const auto good = [](const gneiss::tasks::task_context&) { return gneiss::tasks::task_outcome{}; };
  require(scheduler.submit({.submission_timings = &timing}, good, handle) ==
                  gneiss::tasks::submit_result::invalid_argument &&
              timing.measured && timing.allocate_ms == 0.0,
          "失败提交未覆盖观测或错误语义改变");
  const auto scope = scheduler.make_scope();
  require(scheduler.submit({.scope = scope, .submission_timings = &timing}, good, handle) ==
                  gneiss::tasks::submit_result::success &&
              timing.measured && timing.lock_ms >= 0.0 &&
              timing.work_ms >= timing.allocate_ms + timing.insert_ms + timing.notify_ms,
          "成功提交未提供同次调用的嵌套观测");
  const auto recorded = timing;
  require(scheduler.run_ready().executed == 1U && timing.measured == recorded.measured &&
              timing.lock_ms == recorded.lock_ms && timing.work_ms == recorded.work_ms &&
              timing.allocate_ms == recorded.allocate_ms &&
              timing.insert_ms == recorded.insert_ms && timing.notify_ms == recorded.notify_ms &&
              scheduler.close_scope(scope),
          "任务执行或回收继续修改已经返回的提交观测");
}

}
int main() try {
  cooperative_scheduler_contract();
  submission_observation();
  completion_queue_order();
  {
    gneiss::tasks::task_scheduler scheduler({.mode = gneiss::tasks::execution_mode::cooperative});
    std::jthread other([&] {
      require(scheduler.run_ready().status == gneiss::tasks::drive_status::wrong_thread, "允许了错误线程驱动");
      bool rejected{};
      try {
        scheduler.stop();
      } catch (const std::logic_error&) {
        rejected = true;
      }
      require(rejected, "允许了错误线程关闭");
    });
  }
  for (const auto workers : {1U, 2U, 4U}) {
    dependency_graph(workers);
  }
  dependencies_do_not_block();
  serial_failure_and_cancellation();
  capacity_and_shutdown();
  fair_priority_and_scope_isolation();
  cancelled_serial_middle();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
