// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <chrono>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/platform/files/service.hpp>
#include <gneiss/engine/resource/scene.hpp>
#include <gneiss/engine/resource/scene/scene_validation.hpp>
namespace gneiss::test_support {
inline gneiss::error load_scene_content(gneiss::asset_manager& assets,
                                        gneiss::tasks::task_scheduler& executor,
                                        std::string_view uri, gneiss::scene_content& output,
                                        gneiss::scene_diagnostic& diagnostic,
                                        gneiss::ref<const gneiss::scene>* identity = nullptr) {
  output = {};
  diagnostic = {};
  auto load = [&]() -> gneiss::error {
    auto request = assets.reload<gneiss::scene>(uri);
    if (!request)
      return diagnostic.error = request.error();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
      if (std::chrono::steady_clock::now() >= deadline)
        return diagnostic.error = gneiss::error::internal;
      const auto dispatch = assets.async_files()->dispatch_completions(64);
      if (dispatch.failed())
        return diagnostic.error = dispatch;
      assets.advance();
      (void)executor.run_ready();
      auto loaded = request->take();
      if (!loaded) {
        const auto& failure = loaded.error();
        if (failure.error == gneiss::error::not_ready)
          continue;
        diagnostic = {failure.error, failure.byte_offset, failure.path, failure.message};
        return diagnostic.error;
      }
      if (!(*loaded)->content())
        return diagnostic.error = gneiss::error::invalid_argument;
      output = *(*loaded)->content();
      if (identity)
        *identity = *loaded;
      return gneiss::error::success;
    }
  };
  return load();
}
}
