// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "runtime/application.hpp"
#include "value_result.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
namespace gneiss::test_support {
// 仅测试使用的宿主驱动器；生产库不提供阻塞包装。应用须在测试退出前释放。
inline engine::task_scheduler& application_tasks() {
  static engine::task_scheduler tasks({.workers = 1U});
  return tasks;
}
inline engine::error prepare_scene(runtime::application& app, std::string_view uri,
                                   engine::scene_instantiation_data& prepared,
                                   engine::scene_instances::resource_bindings& bindings,
                                   engine::task_scheduler* tasks = nullptr, bool prefab = false,
                                   bool reload = false) {
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
  auto load = [&]<class T>() -> engine::error {
    auto request = app.assets().load<T>(uri, {.reload = reload});
    if (!request)
      return request.error();
    for (;;) {
      if (!step())
        return engine::error::internal;
      auto loaded = request->take();
      if (!loaded) {
        if (loaded.error().error == engine::error::not_ready)
          continue;
        std::fprintf(stderr, "加载 %s 失败: %d %s %s\n", std::string(uri).c_str(),
                     loaded.error().error.code(), loaded.error().path.c_str(),
                     loaded.error().message.c_str());
        return loaded.error().error;
      }
      auto data = engine::make_instantiation_data(*loaded, request->source_bytes());
      if (!data)
        return data.error().error;
      prepared = std::move(*data);
      return engine::error::success;
    }
  };
  auto loaded_status = prefab ? load.template operator()<engine::prefab>()
                              : load.template operator()<engine::scene>();
  if (loaded_status.failed())
    return loaded_status;
  auto status = engine::error::success;
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
    if (completion.result.failed()) {
      std::fprintf(stderr, "视觉资产 %s 失败: %d %s %s\n", source.uri.c_str(),
                   completion.result.code(), completion.path.c_str(), completion.message.c_str());
      return completion.result;
    }
    for (auto& asset : completion.assets) {
      const auto key = std::string(engine::resource_uri(asset));
      bindings.insert_or_assign(key, std::move(asset));
    }
  }
  return engine::error::success;
}
inline engine::error load_scene(runtime::application& app, std::string_view uri,
                                scene_instance_id* output, engine::task_scheduler* tasks = nullptr,
                                bool reload = false) {
  if (!output)
    return engine::error::invalid_argument;
  *output = {};
  engine::scene_instantiation_data prepared;
  engine::scene_instances::resource_bindings bindings;
  auto status = prepare_scene(app, uri, prepared, bindings, tasks, false, reload);
  return status.failed()
             ? status
             : gneiss::test_support::take_value(
                   app.scenes()->instantiate_prepared(std::move(prepared), bindings), *output);
}
}
