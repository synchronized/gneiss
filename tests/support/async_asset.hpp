// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/prefab.hpp"
#include "engine/resource/scene.hpp"
#include "memory_file_fixture.hpp"
#include "value_result.hpp"
#include <chrono>
#include <stdexcept>
namespace gneiss::test_support {
inline engine::task_scheduler& asset_tasks() {
  static engine::task_scheduler tasks({.workers = 0, .mode = engine::execution_mode::cooperative});
  return tasks;
}
// 测试只取得异步请求提供的身份，不推进 I/O；未配置夹具使用测试共享执行器。
inline engine::result<engine::render_resource_ref>
request_resource(engine::asset_manager& assets, const engine::render_resource_source& source) {
  auto request = assets.load(source);
  if (!request && request.error() == engine::error::invalid_state) {
    auto configured = assets.configure(asset_tasks());
    if (configured.failed())
      return configured;
    request = assets.load(source);
  }
  if (!request)
    return request.error();
  return request->resource();
}
// 仅测试宿主驱动异步回调；生产代码不提供等待包装。
template <class T>
engine::error load_asset(engine::asset_manager& assets, std::string_view uri,
                         engine::asset_ref<const T>& output, bool reload = false,
                         engine::serialization::diagnostic* diagnostic = nullptr) {
  auto& tasks = asset_tasks();
  if (const auto configured = assets.configure(tasks); configured.failed())
    return configured;
  engine::asset_request<T> request;
  auto status =
      gneiss::test_support::assign_value(assets.load<T>(uri, {.reload = reload}), request);
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((status = gneiss::test_support::take_request(request, output, diagnostic)) ==
         engine::error::not_ready) {
    (void)tasks.run_ready();
    (void)assets.async_files()->dispatch_completions(64);
    assets.advance();
    if (std::chrono::steady_clock::now() > deadline)
      throw std::runtime_error("资产请求测试超时");
  }
  return status;
}
// 测试显式取得场景资产的描述快照；生产加载入口始终返回资产对象。
template <class D>
engine::error load_scene_snapshot(engine::asset_manager& assets, std::string_view uri,
                                  engine::ref<const D>& output, bool reload = false,
                                  engine::serialization::diagnostic* diagnostic = nullptr) {
  static_assert(std::is_same_v<D, engine::scene_description> ||
                std::is_same_v<D, engine::prefab_description>);
  using T = std::conditional_t<std::is_same_v<D, engine::scene_description>, engine::scene,
                               engine::prefab>;
  engine::ref<const T> asset;
  auto status = load_asset(assets, uri, asset, reload, diagnostic);
  if (status.ok())
    output = asset->snapshot();
  return status;
}
}
