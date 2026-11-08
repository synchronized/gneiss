// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <thread>
namespace gneiss::test_support {
inline engine::error activate_scene_for_test(runtime::application& app,
                                             engine::task_scheduler& tasks, std::string_view uri,
                                             runtime::scene_load_completion& output) {
  auto attached = app.attach_task_executor(tasks);
  if (attached.failed() && attached != engine::error::invalid_state)
    return attached;
  std::uint64_t request{};
  auto status = app.request_scene(uri, 1, 1, request);
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    (void)tasks.run_ready();
    app.advance_scene_load();
    runtime::scene_load_progress progress;
    if (app.scene_progress(progress)) {
      if (progress.phase == runtime::scene_load_phase::ready) {
        status = app.activate_scene(request, output);
        if (status.failed())
          return status;
        do {
          if (std::chrono::steady_clock::now() >= deadline)
            return engine::error::internal;
          (void)tasks.run_ready();
          app.advance_scene_load();
          std::this_thread::yield();
        } while (app.visual_assets_loading() || !app.can_activate_scene());
        return engine::error::success;
      }
      if (progress.phase == runtime::scene_load_phase::failed)
        return engine::error::invalid_state;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return engine::error::internal;
    std::this_thread::yield();
  }
}

inline engine::error
reload_scene_for_test(runtime::application& app, engine::task_scheduler& tasks,
                      scene_instance_id scene, std::string_view uri, bool prefab,
                      std::vector<engine::scene_object_binding>* objects = nullptr) {
  auto status = app.request_scene_reload(scene, uri, prefab);
  if (status.failed())
    return status;
  if (app.request_scene_reload(scene, uri, prefab) != engine::error::not_ready ||
      app.can_activate_scene())
    return engine::error::internal;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    (void)tasks.run_ready();
    auto completed = app.poll_scene_reload();
    if (!completed)
      return completed.error();
    if (*completed) {
      while (app.visual_assets_loading()) {
        if (std::chrono::steady_clock::now() >= deadline)
          return engine::error::internal;
        (void)tasks.run_ready();
        app.advance_scene_load();
        std::this_thread::yield();
      }
      if (objects)
        *objects = std::move((**completed).objects);
      return (**completed).result;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return engine::error::internal;
    std::this_thread::yield();
  }
}
}
