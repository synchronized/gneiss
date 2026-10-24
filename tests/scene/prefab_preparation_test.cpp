// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/result.hpp"
#include "runtime/application.hpp"
#include "runtime/loading/prefab_preparation.hpp"
#include <chrono>
#include <thread>

bool run(gneiss::engine::execution_mode mode) {
  gneiss::engine::task_scheduler executor({.mode = mode});
  const std::string root = GNEISS_TEST_ASSET_ROOT;
  gneiss::runtime::application_configuration description;
  description.asset_root = root;
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::runtime::application::create(description, application) !=
      gneiss::engine::result::success) {
    return false;
  }
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::unique_ptr<gneiss::runtime::prefab_preparation> request;
    const auto uri =
        attempt == 2 ? "asset://prefabs/missing.prefab.json" : "asset://prefabs/test.prefab.json";
    if (gneiss::runtime::prefab_preparation::create(*application, executor, uri, request) !=
        gneiss::engine::result::success) {
      return false;
    }
    if (attempt == 1) {
      request->cancel();
    }
    bool complete{};
    gneiss::engine::result result = gneiss::engine::result::success;
    gneiss::engine::prepared_scene_description output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!complete && std::chrono::steady_clock::now() < deadline) {
      if (mode == gneiss::engine::execution_mode::cooperative) {
        (void)executor.run_ready();
      }
      result = request->poll(output, complete);
      std::this_thread::yield();
    }
    const auto expected = attempt == 0   ? gneiss::engine::result::success
                          : attempt == 1 ? gneiss::engine::result::invalid_state
                                         : gneiss::engine::result::not_found;
    if (!complete || result != expected || (attempt == 0 && output.prefabs.size() != 1U)) {
      return false;
    }
  }
  return application->shutdown() == gneiss::engine::result::success;
}
int main() {
  return run(gneiss::engine::execution_mode::cooperative) &&
                 run(gneiss::engine::execution_mode::thread_pool)
             ? 0
             : 1;
}
