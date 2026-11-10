// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/asset/asset_manager.hpp"
#include "engine/resource/prefab.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <thread>

bool run(gneiss::engine::execution_mode mode) {
  gneiss::engine::task_scheduler executor({.mode = mode});
  const std::string root = GNEISS_TEST_ASSET_ROOT;
  gneiss::runtime::application_configuration description;
  description.asset_root = root;
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(description),
                                         application) != gneiss::engine::error::success ||
      application->attach_task_executor(executor) != gneiss::engine::error::success) {
    return false;
  }
  for (int attempt = 0; attempt < 3; ++attempt) {

    const auto uri =
        attempt == 2 ? "asset://prefabs/missing.prefab.json" : "asset://prefabs/test.prefab.json";
    auto request = application->assets().reload<gneiss::engine::prefab>(uri);
    if (!request) {
      return false;
    }
    if (attempt == 1) {
      request->cancel();
    }
    bool complete{};
    gneiss::engine::error result = gneiss::engine::error::success;
    gneiss::engine::ref<const gneiss::engine::prefab_description> output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!complete && std::chrono::steady_clock::now() < deadline) {
      if (mode == gneiss::engine::execution_mode::cooperative) {
        (void)executor.run_ready();
      }
      (void)application->advance_loading();
      auto loaded = request->take();
      if (!loaded) {
        result = loaded.error().error;
        complete = result != gneiss::engine::error::not_ready;
      } else {
        output = (*loaded)->snapshot();
        result = output ? gneiss::engine::error::success : gneiss::engine::error::invalid_argument;
        complete = true;
      }
      std::this_thread::yield();
    }
    const auto expected = attempt == 0   ? gneiss::engine::error::success
                          : attempt == 1 ? gneiss::engine::error::invalid_state
                                         : gneiss::engine::error::not_found;
    if (!complete || result != expected || (attempt == 0 && (!output || output->objects.empty()))) {
      return false;
    }
  }
  return application->shutdown() == gneiss::engine::error::success;
}
int main() {
  return run(gneiss::engine::execution_mode::cooperative) &&
                 run(gneiss::engine::execution_mode::thread_pool)
             ? 0
             : 1;
}
