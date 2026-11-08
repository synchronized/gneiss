// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/function/scene/scene_preparation.hpp"
#include "engine/platform/files/file_service.hpp"
#include <chrono>
namespace gneiss::test_support {
inline engine::error
prepare_scene_for_test(engine::asset_manager& assets, engine::task_scheduler& executor, bool prefab,
                       std::string_view uri, engine::scene_instantiation_data& output,
                       engine::scene_diagnostic& diagnostic, const std::function<bool()>& cancelled,
                       engine::scene_prepare_limits limits = {}) {
  output = {};
  diagnostic = {};
  if (cancelled && cancelled())
    return diagnostic.error = engine::error::invalid_state;
  auto load = [&]<class T>() -> engine::error {
    auto request = assets.reload<T>(uri);
    if (!request)
      return diagnostic.error = request.error();
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
      auto loaded = request->take();
      if (!loaded) {
        const auto& failure = loaded.error();
        if (failure.error == engine::error::not_ready)
          continue;
        diagnostic = {failure.error, failure.byte_offset, failure.path, failure.message};
        return diagnostic.error;
      }
      auto data = engine::make_instantiation_data(*loaded, request->source_bytes(), limits);
      if (!data) {
        diagnostic = std::move(data).error();
        return diagnostic.error;
      }
      output = std::move(*data);
      return engine::error::success;
    }
  };
  return prefab ? load.template operator()<engine::prefab>()
                : load.template operator()<engine::scene>();
}
}
