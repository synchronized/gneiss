// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <thread>
namespace gneiss::test_support {
// 仅测试使用的宿主驱动器；生产库不提供阻塞包装。应用须在测试退出前释放。
inline engine::task_scheduler& application_tasks() {
  static engine::task_scheduler tasks({.workers = 1U});
  return tasks;
}
inline engine::error prepare_scene(runtime::application& app, std::string_view uri,
                                   engine::scene_instantiation_data& prepared,
                                   engine::scene_instances::asset_bindings& bindings,
                                   engine::task_scheduler* tasks = nullptr, bool prefab = false) {
  if (!tasks)
    tasks = &application_tasks();
  const auto attached = app.attach_task_executor(*tasks);
  if (attached.failed() && attached != engine::error::invalid_state)
    return attached;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  const auto step = [&] {
    if (tasks->mode() == engine::execution_mode::cooperative)
      (void)tasks->run_ready();
    app.advance_scene_load();
    std::this_thread::yield();
    return std::chrono::steady_clock::now() < deadline;
  };
  engine::scene_description_request request(app.assets(), std::string(uri), prefab);
  engine::scene_description_inputs inputs;
  engine::scene_diagnostic diagnostic;
  for (;;) {
    if (!step())
      return engine::error::internal;
    const auto status = request.advance(inputs, diagnostic);
    if (status == engine::error::not_ready)
      continue;
    if (status.failed())
      return status;
    break;
  }
  auto status = engine::prepare_loaded_scene(inputs, prefab ? uri : std::string_view{}, prepared,
                                             diagnostic, {});
  if (status.failed())
    return status;
  bindings.clear();
  for (const auto& source : prepared.assets) {
    std::uint64_t id{};
    status = app.request_assets(std::span(&source, 1), 1, 1, id, false);
    if (status.failed())
      return status;
    runtime::asset_load_completion completion;
    bool ready{};
    while (!ready) {
      if (!step())
        return engine::error::internal;
      status = app.poll_assets(completion, ready);
      if (status.failed())
        return status;
    }
    if (completion.result.failed())
      return completion.result;
    for (auto& asset : completion.assets) {
      const auto key = std::string(engine::asset_uri(asset));
      bindings.insert_or_assign(key, std::move(asset));
    }
  }
  return engine::error::success;
}
inline engine::error load_scene(runtime::application& app, std::string_view uri,
                                scene_instance_id* output,
                                engine::task_scheduler* tasks = nullptr) {
  if (!output)
    return engine::error::invalid_argument;
  *output = {};
  engine::scene_instantiation_data prepared;
  engine::scene_instances::asset_bindings bindings;
  auto status = prepare_scene(app, uri, prepared, bindings, tasks);
  return status.failed()
             ? status
             : app.scenes()->instantiate_prepared(std::move(prepared), bindings, *output);
}
}
