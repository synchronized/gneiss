// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "scene_instantiation.hpp"
#include "value_result.hpp"
#include <chrono>
#include <cstdio>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/instantiation.hpp>
#include <gneiss/engine/resource/scene.hpp>
#include <thread>
namespace gneiss::test_support {
/** 测试宿主的实例化回执；视觉批次由调用方驱动和领取，不隐藏等待或场景替换。 */
struct scene_activation {
  gneiss::scene_instantiation_result instance;
  gneiss::asset_batch_request visuals;
};
inline gneiss::result<scene_activation> activate_scene(runtime::application& app,
                                                       gneiss::world& world,
                                                       const gneiss::scene_content& content) {
  auto composed = compose(content);
  if (!composed)
    return composed.error();
  auto dependencies = gneiss::scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  scene_activation output;
  if (!dependencies->empty()) {
    auto submitted = app.assets().load_batch(*dependencies);
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
  auto destination = world.create_scene();
  if (!destination)
    return destination.error();
  auto built = gneiss::instantiate(*destination, *composed, bindings);
  if (!built) {
    destination->unload();
    return built.error();
  }
  entity_id camera;
  for (std::size_t i = 0; i < composed->objects.size(); ++i) {
    auto* description = composed->objects[i].effective.components.get<gneiss::camera_description>();
    if (description && description->is_primary)
      camera = built->objects[i].entity;
  }
  if (camera) {
    auto selected = gneiss::set_active_camera(world, camera);
    if (selected.failed()) {
      destination->unload();
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
inline gneiss::error prepare_scene(runtime::application& app, std::string_view uri,
                                   gneiss::scene_content& prepared,
                                   gneiss::scene_resource_bindings& bindings,
                                   gneiss::tasks::task_scheduler* tasks = nullptr,
                                   gneiss::ref<const gneiss::scene>* identity = nullptr,
                                   bool reload = false) {
  if (!tasks)
    tasks = &application_tasks();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  const auto step = [&] {
    if (tasks->mode() == gneiss::tasks::execution_mode::cooperative)
      (void)tasks->run_ready();
    const auto frame = app.step();
    if (!frame)
      return frame.error();
    if (*frame == app::lifecycle::stopping || *frame == app::lifecycle::stopped)
      return error::cancelled;
    std::this_thread::yield();
    return std::chrono::steady_clock::now() < deadline ? gneiss::error{gneiss::error::success}
                                                       : gneiss::error{gneiss::error::internal};
  };
  auto load = [&]() -> gneiss::error {
    auto request = app.assets().load<gneiss::scene>(uri, {.reload = reload});
    if (!request)
      return request.error();
    for (;;) {
      if (const auto advanced = step(); advanced.failed())
        return advanced;
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
  auto loaded_status = load();
  if (loaded_status.failed())
    return loaded_status;
  bindings.clear();
  auto composed = compose(prepared);
  if (!composed)
    return composed.error();
  auto dependencies = gneiss::scene_dependencies(*composed);
  if (!dependencies)
    return dependencies.error();
  if (dependencies->empty())
    return gneiss::error::success;
  // 一次提交整个视觉批次；测试宿主只负责等待，不逐资产重做批次调度。
  auto request = app.assets().load_batch(*dependencies);
  if (!request)
    return request.error();
  for (;;) {
    if (const auto advanced = step(); advanced.failed())
      return advanced;
    auto completion = request->take();
    if (!completion) {
      if (completion.error() == gneiss::error::not_ready)
        continue;
      return completion.error();
    }
    if (completion->result.failed()) {
      std::fprintf(stderr, "视觉资产批次失败: %d %s %s\n", completion->result.code(),
                   completion->path.c_str(), completion->message.c_str());
      return completion->result;
    }
    for (auto& asset : completion->assets) {
      const auto key = std::string(gneiss::resource_uri(asset));
      bindings.insert_or_assign(key, std::move(asset));
    }
    break;
  }
  return gneiss::error::success;
}
inline gneiss::error load_scene(runtime::application& app, gneiss::world& world,
                                std::string_view uri, scene_instance* output,
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
  auto receipt = scene_fixture::instantiate_scene(world, prepared, bindings);
  if (!receipt)
    return receipt.error();
  *output = receipt->instance;
  if (objects)
    *objects = std::move(receipt->objects);
  return gneiss::error::success;
}
}
