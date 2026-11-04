// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "runtime/application.hpp"
#include <chrono>
namespace gneiss::test_support {
inline engine::error load_input_for_test(runtime::application& app,
                                         engine::task_scheduler& scheduler, std::string_view uri) {
  auto status = app.begin_action_map(uri);
  if (status == engine::error::not_ready) {
    status = app.attach_task_executor(scheduler);
    if (status.failed())
      return status;
    status = app.begin_action_map(uri);
  }
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((status = app.action_map_status()) == engine::error::not_ready) {
    if (std::chrono::steady_clock::now() >= deadline)
      return engine::error::internal;
    (void)scheduler.run_ready();
    const auto frame = app.run(1);
    if (frame.failed())
      return frame;
  }
  return status;
}
}
