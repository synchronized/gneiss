// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/diagnostics/loop_timing.hpp"
#include "engine/core/tasks/task_scheduler.hpp"

namespace gneiss::diagnostics {

// 宿主调度器可能位于另一动态库，不能依赖它与调用方共享 thread_local。
// 仅同步借用栈上结果，返回后合并到调用方的循环记录，不保存第二套调度状态。
inline tasks::submit_result submit_observed(tasks::task_executor& executor,
                                            tasks::task_description description,
                                            tasks::task_executor::task_function function,
                                            tasks::task_handle& output) {
  if (active_loop_times == nullptr) {
    return executor.submit(std::move(description), std::move(function), output);
  }
  struct observation {
    stage_times* destination;
    tasks::task_submission_timings timings{};
    ~observation() {
      if (!timings.measured) {
        return;
      }
      (*destination)[static_cast<std::size_t>(loop_stage::task_submit_lock)] += timings.lock_ms;
      (*destination)[static_cast<std::size_t>(loop_stage::task_submit_work)] += timings.work_ms;
      (*destination)[static_cast<std::size_t>(loop_stage::task_submit_allocate)] +=
          timings.allocate_ms;
      (*destination)[static_cast<std::size_t>(loop_stage::task_submit_insert)] += timings.insert_ms;
      (*destination)[static_cast<std::size_t>(loop_stage::task_submit_notify)] += timings.notify_ms;
    }
  } current{active_loop_times};
  description.submission_timings = &current.timings;
  return measure(loop_stage::task_submit_dispatch, [&] {
    return executor.submit(std::move(description), std::move(function), output);
  });
}

} // namespace gneiss::diagnostics
