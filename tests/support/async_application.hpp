// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "application/application.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/camera_selection.hpp"
#include "engine/function/scene/instantiation.hpp"
#include "engine/resource/scene.hpp"
#include "scene_access.hpp"
#include "scene_instantiation.hpp"
#include "value_result.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
namespace gneiss::test_support {
/** 测试宿主的实例化回执；视觉批次由调用方驱动和领取，不隐藏等待或场景替换。 */
struct scene_activation {
  gneiss::scene_instantiation_result instance;
  gneiss::asset_batch_request visuals;
};
inline gneiss::result<scene_activation> activate_scene(runtime::application& app,
                                                       const gneiss::scene_content& content) {
  auto composed = compose(content);
  if (!composed)
    return composed.error();
  auto dependencies = gneiss::scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  scene_activation output;
  if (!dependencies->empty()) {
    auto submitted = app.load_assets(*dependencies);
    if (!submitted)
      return submitted.error();
    output.visuals = std::move(*submitted);
  }
  gneiss::scene_resource_bindings bindings;
  for (const auto& dependency : *dependencies) {
    auto request = app.assets().load(dependency);
    if (!request)
      return request.error();
    bindings.emplace(dependency.uri, request->resource());
  }
  auto destination = app.native_world()->create_scene();
  if (!destination)
    return destination.error();
  auto built = gneiss::instantiate(*destination, *composed, bindings);
  if (!built) {
    (void)destination->unload();
    return built.error();
  }
  entity_id camera;
  for (std::size_t i = 0; i < composed->objects.size(); ++i) {
    auto* description = composed->objects[i].effective.components.get<gneiss::camera_description>();
    if (description && description->is_primary)
      camera = built->objects[i].entity;
  }
  if (camera) {
    auto selected = gneiss::set_active_camera(*app.native_world(), camera);
    if (selected.failed()) {
      (void)destination->unload();
      return selected;
    }
  }
  output.instance = std::move(*built);
  return output;
}
// 仅测试使用的宿主驱动器；生产库不提供阻塞包装。应用须在测试退出前释放。
inline gneiss::tasks::task_scheduler& application_tasks() {
  static gneiss::tasks::task_scheduler tasks({.workers = 1U});
  return tasks;
}
inline gneiss::error
prepare_scene(runtime::application& app, std::string_view uri, gneiss::scene_content& prepared,
              gneiss::scene_resource_bindings& bindings, gneiss::tasks::task_scheduler* tasks = nullptr,
              gneiss::ref<const gneiss::scene>* identity = nullptr, bool reload = false) {
  if (!tasks)
    tasks = &application_tasks();
  const auto attached = app.attach_task_executor(*tasks);
  if (attached.failed() && attached != gneiss::error::invalid_state)
    return attached;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  const auto step = [&] {
    if (tasks->mode() == gneiss::tasks::execution_mode::cooperative)
      (void)tasks->run_ready();
    (void)app.run(1);
    std::this_thread::yield();
    return std::chrono::steady_clock::now() < deadline;
  };
  auto load = [&]<class T>() -> gneiss::error {
    auto request = app.assets().load<T>(uri, {.reload = reload});
    if (!request)
      return request.error();
    for (;;) {
      if (!step())
        return gneiss::error::internal;
      auto loaded = request->take();
      if (!loaded) {
        if (loaded.error().error == gneiss::error::not_ready)
          continue;
        std::fprintf(stderr, "加载 %s 失败: %d %s %s\n", std::string(uri).c_str(),
                     loaded.error().error.code(), loaded.error().path.c_str(),
                     loaded.error().message.c_str());
        return loaded.error().error;
      }
      if (!(*loaded)->content())
        return gneiss::error::invalid_argument;
      prepared = *(*loaded)->content();
      if (identity)
        *identity = *loaded;
      return gneiss::error::success;
    }
  };
  auto loaded_status = load.template operator()<gneiss::scene>();
  if (loaded_status.failed())
    return loaded_status;
  auto status = gneiss::error::success;
  bindings.clear();
  auto composed = compose(prepared);
  if (!composed)
    return composed.error();
  auto dependencies = gneiss::scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  for (const auto& source : *dependencies) {
    gneiss::asset_batch_request request;
    status = gneiss::test_support::take_value(app.load_assets(std::span(&source, 1)), request);
    if (status.failed())
      return status;
    gneiss::asset_load_completion completion;
    bool ready{};
    while (!ready) {
      if (!step())
        return gneiss::error::internal;
      status = gneiss::test_support::receive_request(request, completion, ready);
      if (status.failed())
        return status;
    }
    if (completion.result.failed()) {
      std::fprintf(stderr, "视觉资产 %s 失败: %d %s %s\n", source.uri.c_str(),
                   completion.result.code(), completion.path.c_str(), completion.message.c_str());
      return completion.result;
    }
    for (auto& asset : completion.assets) {
      const auto key = std::string(gneiss::resource_uri(asset));
      bindings.insert_or_assign(key, std::move(asset));
    }
  }
  return gneiss::error::success;
}
inline gneiss::error load_scene(runtime::application& app, std::string_view uri,
                                scene_instance* output,
                                gneiss::tasks::task_scheduler* tasks = nullptr, bool reload = false,
                                std::vector<gneiss::scene_object_binding>* objects = nullptr) {
  if (!output)
    return gneiss::error::invalid_argument;
  *output = {};
  gneiss::scene_content prepared;
  gneiss::scene_resource_bindings bindings;
  auto status = prepare_scene(app, uri, prepared, bindings, tasks, nullptr, reload);
  if (status.failed())
    return status;
  auto receipt = scene_fixture::instantiate_scene(*app.native_world(), prepared, bindings);
  if (!receipt)
    return receipt.error();
  *output = receipt->instance;
  if (objects)
    *objects = std::move(receipt->objects);
  return gneiss::error::success;
}
}
