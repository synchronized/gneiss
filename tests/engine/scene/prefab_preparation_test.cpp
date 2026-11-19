// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/value_result.hpp"
#include <chrono>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/asset/asset_manager.hpp>
#include <gneiss/engine/resource/prefab.hpp>
#include <thread>

bool run(gneiss::tasks::execution_mode mode) {
  gneiss::tasks::task_scheduler executor({.mode = mode});
  const std::string root = GNEISS_TEST_ASSET_ROOT;
  gneiss::runtime::application_configuration description;
  description.asset_root = root;
  description.executor = &executor;
  std::shared_ptr<gneiss::runtime::application> application;
  if (gneiss::test_support::assign_value(gneiss::runtime::application::create(description),
                                         application) != gneiss::error::success) {
    return false;
  }
  for (int attempt = 0; attempt < 3; ++attempt) {

    const auto uri =
        attempt == 2 ? "asset://prefabs/missing.prefab.json" : "asset://prefabs/test.prefab.json";
    auto request = application->assets().reload<gneiss::prefab>(uri);
    if (!request) {
      return false;
    }
    if (attempt == 1) {
      request->cancel();
    }
    bool complete{};
    gneiss::error result = gneiss::error::success;
    gneiss::ref<const gneiss::prefab_description> output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!complete && std::chrono::steady_clock::now() < deadline) {
      if (mode == gneiss::tasks::execution_mode::cooperative) {
        (void)executor.run_ready();
      }
      (void)application->run(1);
      auto loaded = request->take();
      if (!loaded) {
        result = loaded.error().error;
        complete = result != gneiss::error::not_ready;
      } else {
        output = (*loaded)->snapshot();
        result = output ? gneiss::error::success : gneiss::error::invalid_argument;
        complete = true;
      }
      std::this_thread::yield();
    }
    const auto expected = attempt == 0   ? gneiss::error::success
                          : attempt == 1 ? gneiss::error::invalid_state
                                         : gneiss::error::not_found;
    if (!complete || result != expected || (attempt == 0 && (!output || output->objects.empty()))) {
      return false;
    }
  }
  return application->shutdown() == gneiss::error::success;
}
int main() {
  return run(gneiss::tasks::execution_mode::cooperative) &&
                 run(gneiss::tasks::execution_mode::thread_pool)
             ? 0
             : 1;
}
