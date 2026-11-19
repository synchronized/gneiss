// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <chrono>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
namespace gneiss::test_support {
inline gneiss::error load_input_for_test(runtime::application& app,
                                         gneiss::tasks::task_scheduler& scheduler,
                                         std::string_view uri) {
  auto status = app.begin_action_map(uri);
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((status = app.action_map_status()) == gneiss::error::not_ready) {
    if (std::chrono::steady_clock::now() >= deadline)
      return gneiss::error::internal;
    (void)scheduler.run_ready();
    const auto frame = app.run(1);
    if (frame.failed())
      return frame;
  }
  return status;
}
}
