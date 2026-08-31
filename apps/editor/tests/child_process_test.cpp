// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "child_process.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

int main() try {
  gneiss::child_process process;
  gneiss::child_process_start_info missing;
  missing.executable = std::filesystem::path{GNEISS_TEST_RUNTIME}.parent_path() / "missing-runtime";
  if (process.start(missing) != gneiss::result::not_found || process.is_running()) {
    return 1;
  }

  gneiss::child_process_start_info info;
  info.executable = GNEISS_TEST_RUNTIME;
  info.arguments = {"--smoke", "--project", GNEISS_TEST_PROJECT_ROOT};
  if (process.start(info) != gneiss::result::success || !process.has_started() ||
      process.start(info) != gneiss::result::invalid_state) {
    return 2;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (process.is_running() && std::chrono::steady_clock::now() < deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  if (process.is_running() || process.exit_code() != 0 ||
      process.output().find("stage=shutdown") == std::string::npos ||
      process.terminate() != gneiss::result::not_ready) {
    return 3;
  }
  return 0;
} catch (...) {
  return 99;
}
