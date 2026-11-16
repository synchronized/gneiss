// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include "host/runtime/runtime_scene_loader.hpp"
#include <chrono>
#include <thread>
namespace gneiss::test_support {
inline gneiss::error activate_scene_for_test(runtime::application& app,
                                             gneiss::tasks::task_scheduler& tasks, std::string_view uri,
                                             host::scene_open_result& output) {
  auto attached = app.attach_task_executor(tasks);
  if (attached.failed() && attached != gneiss::error::invalid_state)
    return attached;
  host::runtime_scene_loader loader(app);
  loader.after_activate = [&](const auto& result) {
    output = result;
    return gneiss::error{gneiss::error::success};
  };
  auto status = loader.request({.session = 1, .revision = 1, .uri = std::string(uri)}, 1);
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (loader.busy() || loader.visual_loading()) {
    (void)tasks.run_ready();
    auto stepped = app.run(1);
    if (stepped.failed())
      return stepped;
    stepped = loader.advance();
    if (stepped.failed())
      return stepped;
    if (std::chrono::steady_clock::now() >= deadline)
      return gneiss::error::internal;
    std::this_thread::yield();
  }
  return loader.last_result();
}
inline gneiss::error
reload_scene_for_test(runtime::application& app, gneiss::tasks::task_scheduler& tasks,
                      gneiss::ref<host::scene_reload_state> source, std::string_view uri,
                      bool prefab, std::vector<gneiss::scene_object_binding>* objects = nullptr) {
  host::runtime_scene_loader loader(app);
  auto submitted = loader.reload(source, uri, prefab);
  if (submitted.failed())
    return submitted;
  if (loader.reload(source, uri, prefab) != gneiss::error::not_ready)
    return gneiss::error::internal;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    (void)tasks.run_ready();
    (void)app.run(1);
    auto stepped = loader.advance();
    if (stepped.failed())
      return stepped;
    auto completed = loader.take_reload();
    if (!completed && completed.error() != gneiss::error::not_ready)
      return completed.error();
    if (completed) {
      while (loader.visual_loading()) {
        if (std::chrono::steady_clock::now() >= deadline)
          return gneiss::error::internal;
        (void)tasks.run_ready();
        (void)app.run(1);
        (void)loader.advance();
        std::this_thread::yield();
      }
      if (objects)
        *objects = std::move(completed->objects);
      return completed->result;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return gneiss::error::internal;
    std::this_thread::yield();
  }
}
}
