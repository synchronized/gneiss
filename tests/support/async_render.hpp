// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_asset.hpp"
#include "value_result.hpp"
#include <chrono>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/platform/files/service.hpp>
#include <stdexcept>
namespace gneiss::test_support {
inline gneiss::tasks::task_scheduler& render_tasks() { return asset_tasks(); }
inline gneiss::error load_render(gneiss::asset_manager& loader, gneiss::render_resource_kind type,
                                 std::string_view uri, gneiss::render_resource_ref& output,
                                 gneiss::asset_diagnostic& diagnostic) {
  auto status = gneiss::test_support::take_value(loader.find({std::string(uri), type}), output);
  if (status != gneiss::error::not_found)
    return status;
  auto& tasks = render_tasks();
  status = loader.configure(tasks);
  if (status.failed())
    return status;
  const gneiss::render_resource_source source{std::string(uri), type};
  auto request = loader.load(source);
  if (!request)
    return request.error();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    (void)tasks.run_ready();
    if (const auto dispatched = loader.async_files()->dispatch_completions(64); dispatched.failed())
      return dispatched;
    loader.advance();
    if (request->status() != gneiss::error::not_ready) {
      auto completed = request->take();
      if (!completed) {
        diagnostic = completed.error();
        loader.collect_unused();
        return diagnostic.error;
      }
      output = std::move(*completed);
      diagnostic = {};
      return gneiss::error::success;
    }
    if (std::chrono::steady_clock::now() > deadline)
      throw std::runtime_error("异步渲染资产测试超时");
  }
}
}
