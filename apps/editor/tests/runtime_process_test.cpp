// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include <chrono>
#include <filesystem>
#include <thread>

int main() try {
  gneiss::editor::runtime_process process;
  gneiss::editor::runtime_launch_request request{std::filesystem::path{GNEISS_TEST_PROJECT_ROOT}};
  const std::filesystem::path executable{GNEISS_TEST_RUNTIME};
  if (process.start(executable, request) != gneiss::result::success || !process.is_running() ||
      process.start(executable, request) != gneiss::result::invalid_state) {
    return 1;
  }

  const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < startup_deadline &&
         process.output().find("stage=startup_scene") == std::string::npos) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!process.is_running() || process.output().find("stage=startup_scene") == std::string::npos ||
      process.request_stop() != gneiss::result::success) {
    return 2;
  }

  const auto stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < stop_deadline && process.is_running()) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  if (process.is_running() || process.exit_code() != 0 ||
      process.output().find("stage=stop_request") == std::string::npos ||
      process.output().find("stage=shutdown") == std::string::npos ||
      process.request_stop() != gneiss::result::not_ready) {
    return 3;
  }
  return 0;
} catch (...) {
  return 99;
}
