// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/platform/files/file_service.hpp"
#include "engine/resource/scene.hpp"
#include "engine/resource/scene/scene_validation.hpp"
#include <chrono>
namespace gneiss::test_support {
inline engine::error load_scene_content(engine::asset_manager& assets,
                                        engine::task_scheduler& executor, std::string_view uri,
                                        engine::scene_content& output,
                                        engine::scene_diagnostic& diagnostic,
                                        engine::ref<const engine::scene>* identity = nullptr) {
  output = {};
  diagnostic = {};
  auto load = [&]<class T>() -> engine::error {
    auto request = assets.reload<T>(uri);
    if (!request)
      return diagnostic.error = request.error();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
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
      if (!(*loaded)->content())
        return diagnostic.error = engine::error::invalid_argument;
      output = *(*loaded)->content();
      if (identity)
        *identity = *loaded;
      return engine::error::success;
    }
  };
  return load.template operator()<engine::scene>();
}
}
