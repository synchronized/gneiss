// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <chrono>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/input/service.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>
namespace gneiss::test_support {
inline gneiss::error load_input_for_test(app::application& app, gneiss::tasks::scheduler& scheduler,
                                         std::string_view uri) {
  const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  if (!app.async_files()) {
    for (;;) {
      auto started = app.step();
      if (!started)
        return started.error();
      if (*started == app::lifecycle::running)
        break;
      if (*started != app::lifecycle::starting)
        return error::cancelled;
      if (std::chrono::steady_clock::now() >= startup_deadline)
        return error::internal;
      (void)scheduler.run_ready();
    }
  }
  auto request = app.assets().reload<input::action_map>(uri);
  if (!request)
    return request.error();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (request->status() == gneiss::error::not_ready) {
    if (std::chrono::steady_clock::now() >= deadline)
      return gneiss::error::internal;
    (void)scheduler.run_ready();
    const auto frame = app.step();
    if (!frame)
      return frame.error();
    if (*frame == app::lifecycle::stopping || *frame == app::lifecycle::stopped)
      return error::cancelled;
  }
  auto loaded = request->take();
  return loaded ? app.input().replace_action_map(*(*loaded)->snapshot()) : loaded.error().error;
}
}
