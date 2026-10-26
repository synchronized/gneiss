// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <thread>
namespace gneiss::test_support {
inline engine::result reload_scene_for_test(runtime::application& app,
                                            engine::task_scheduler& tasks, scene_instance_id scene,
                                            std::string_view uri, bool prefab) {
  auto status = app.request_scene_reload(scene, uri, prefab);
  if (status.failed())
    return status;
  if (app.request_scene_reload(scene, uri, prefab) != engine::result::not_ready ||
      app.can_activate_scene())
    return engine::result::internal;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    (void)tasks.run_ready();
    bool ready{};
    engine::result result;
    status = app.poll_scene_reload(result, ready);
    if (status.failed())
      return status;
    if (ready)
      return result;
    if (std::chrono::steady_clock::now() >= deadline)
      return engine::result::internal;
    std::this_thread::yield();
  }
}
}
