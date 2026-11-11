// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "application/application.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "value_result.hpp"
#include <chrono>
#include <thread>
namespace gneiss::test_support {
inline engine::error activate_scene_for_test(runtime::application& app,
                                             engine::task_scheduler& tasks, std::string_view uri,
                                             runtime::scene_load_completion& output) {
  auto attached = app.attach_task_executor(tasks);
  if (attached.failed() && attached != engine::error::invalid_state)
    return attached;
  gneiss::runtime::scene_open_request request;
  auto status = gneiss::test_support::take_value(app.scenes().open(uri), request);
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    (void)tasks.run_ready();
    (void)app.run(1);
    runtime::scene_load_progress progress;
    if (gneiss::test_support::observe_request(request, progress)) {
      if (progress.phase == runtime::scene_load_phase::ready) {
        status =
            gneiss::test_support::assign_value(app.scenes().activate(std::move(request)), output);
        if (status.failed())
          return status;
        do {
          if (std::chrono::steady_clock::now() >= deadline)
            return engine::error::internal;
          (void)tasks.run_ready();
          (void)app.run(1);
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
                      engine::ref<runtime::scene_reload_state> source, std::string_view uri,
                      bool prefab, std::vector<engine::scene_object_binding>* objects = nullptr) {
  auto submitted = app.scenes().reload(source, uri, prefab);
  if (!submitted)
    return submitted.error();
  auto reload_request = std::move(*submitted);
  if (app.scenes().reload(source, uri, prefab).error() != engine::error::not_ready ||
      app.can_activate_scene())
    return engine::error::internal;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    (void)tasks.run_ready();
    (void)app.run(1);
    auto completed = reload_request.take();
    if (!completed && completed.error() != engine::error::not_ready)
      return completed.error();
    if (completed) {
      while (app.visual_assets_loading()) {
        if (std::chrono::steady_clock::now() >= deadline)
          return engine::error::internal;
        (void)tasks.run_ready();
        (void)app.run(1);
        std::this_thread::yield();
      }
      if (objects)
        *objects = std::move(completed->objects);
      return completed->result;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return engine::error::internal;
    std::this_thread::yield();
  }
}
}
