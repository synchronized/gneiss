// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "async_asset.hpp"
#include "engine/function/resource/resource_service.hpp"
#include "engine/platform/files/file_service.hpp"
#include "runtime/loading/resource_load_group.hpp"
#include <chrono>
#include <stdexcept>
namespace gneiss::test_support {
inline engine::task_scheduler& render_tasks() { return asset_tasks(); }
inline engine::result load_render(engine::resource_service& loader, engine::asset_type type,
                                  std::string_view uri, engine::render_asset_ref& output,
                                  engine::asset_diagnostic& diagnostic) {
  auto status = loader.acquire_asset(type, uri, output, diagnostic);
  if (status != engine::result::not_found)
    return status;
  auto& tasks = render_tasks();
  status = loader.configure_loading(tasks);
  if (status.failed())
    return status;
  const engine::asset_source source{std::string(uri), type};
  runtime::resource_load_group group{loader};
  std::uint64_t request{};
  status = group.submit_assets(std::span(&source, 1), 1, 1, request, false);
  if (status.failed())
    return status;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    (void)tasks.run_ready();
    (void)loader.assets().async_files()->dispatch_completions(64);
    loader.assets().advance();
    group.advance();
    runtime::asset_load_completion completion;
    if (group.take(completion)) {
      diagnostic = {completion.result, completion.byte_offset, std::move(completion.path),
                    std::move(completion.message)};
      if (completion.result.failed()) {
        completion.assets.clear();
        loader.collect_unused();
        return completion.result;
      }
      return loader.acquire_asset(type, uri, output, diagnostic);
    }
    if (std::chrono::steady_clock::now() > deadline)
      throw std::runtime_error("异步渲染资产测试超时");
  }
}
}
