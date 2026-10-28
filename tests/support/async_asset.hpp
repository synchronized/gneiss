// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/platform/files/file_service.hpp"
#include "memory_file_fixture.hpp"
#include <chrono>
#include <stdexcept>
namespace gneiss::test_support {
inline engine::task_scheduler& asset_tasks() {
  static engine::task_scheduler tasks({.workers = 0, .mode = engine::execution_mode::cooperative});
  return tasks;
}
// 仅测试宿主驱动异步回调；生产代码不提供等待包装。
template <class T>
engine::result load_asset(engine::asset_manager& assets, std::string_view uri,
                          engine::asset_ref<const T>& output, bool reload = false,
                          engine::meta::serialization_diagnostic* diagnostic = nullptr) {
  auto& tasks = asset_tasks();
  assets.attach_task_executor(tasks);
  engine::asset_request<T> request;
  auto status = assets.load(uri, request, reload);
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while ((status = request.take(output, diagnostic)) == engine::result::not_ready) {
    (void)tasks.run_ready();
    (void)assets.async_files()->dispatch_completions(64);
    assets.advance();
    if (std::chrono::steady_clock::now() > deadline)
      throw std::runtime_error("资产请求测试超时");
  }
  return status;
}
}
