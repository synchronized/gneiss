// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime/application_registry.hpp"
#include "runtime/loading/prefab_preparation.hpp"
#include <chrono>
#include <thread>

bool run(gneiss::tasks::execution_mode mode) {
  gneiss::tasks::task_scheduler executor({.mode = mode});
  const std::string root = GNEISS_TEST_ASSET_ROOT;
  auto description = gneiss_application_desc GNEISS_APPLICATION_DESC_INIT;
  description.asset_root = root.data();
  description.asset_root_length = static_cast<std::uint32_t>(root.size());
  gneiss_application application{};
  if (gneiss_application_create(&description, &application) != GNEISS_SUCCESS) {
    return false;
  }
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::unique_ptr<gneiss::runtime::prefab_preparation> request;
    const auto uri =
        attempt == 2 ? "asset://prefabs/missing.prefab.json" : "asset://prefabs/test.prefab.json";
    if (gneiss::runtime::prefab_preparation::create(*gneiss::runtime::find_application(application),
                                                    executor, uri, request) != GNEISS_SUCCESS) {
      return false;
    }
    if (attempt == 1) {
      request->cancel();
    }
    bool complete{};
    gneiss_result result = GNEISS_SUCCESS;
    gneiss::scene_internal::prepared_scene_description output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!complete && std::chrono::steady_clock::now() < deadline) {
      if (mode == gneiss::tasks::execution_mode::cooperative) {
        (void)executor.run_ready();
      }
      result = request->poll(output, complete);
      std::this_thread::yield();
    }
    const auto expected = attempt == 0   ? GNEISS_SUCCESS
                          : attempt == 1 ? GNEISS_ERROR_INVALID_STATE
                                         : GNEISS_ERROR_NOT_FOUND;
    if (!complete || result != expected || (attempt == 0 && output.prefabs.size() != 1U)) {
      return false;
    }
  }
  return gneiss_application_destroy(application) == GNEISS_SUCCESS;
}
int main() {
  return run(gneiss::tasks::execution_mode::cooperative) &&
                 run(gneiss::tasks::execution_mode::thread_pool)
             ? 0
             : 1;
}
