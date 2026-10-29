// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/resource/asset_manager.hpp"
#include "engine/function/scene/scene_load_preparation.hpp"
#include "engine/platform/files/file_service.hpp"
#include <chrono>
namespace gneiss::test_support {
inline engine::error
prepare_scene_for_test(engine::asset_manager& assets, engine::task_scheduler& executor, bool prefab,
                       std::string_view uri, engine::prepared_scene_description& output,
                       engine::scene_diagnostic& diagnostic, const std::function<bool()>& cancelled,
                       engine::scene_prepare_limits limits = {}) {
  output = {};
  diagnostic = {};
  engine::scene_description_request request(assets, std::string(uri), prefab, limits);
  engine::scene_description_inputs inputs;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    if (cancelled && cancelled())
      return diagnostic.error = engine::error::invalid_state;
    if (std::chrono::steady_clock::now() >= deadline)
      return diagnostic.error = engine::error::internal;
    const auto dispatch = assets.async_files()->dispatch_completions(64);
    if (dispatch.failed())
      return diagnostic.error = dispatch;
    assets.advance();
    (void)executor.run_ready();
    const auto status = request.advance(inputs, diagnostic);
    if (status == engine::error::not_ready)
      continue;
    if (status.failed())
      return status;
    return engine::prepare_loaded_scene(inputs, prefab ? uri : std::string_view{}, output,
                                        diagnostic, cancelled, limits);
  }
}
}
