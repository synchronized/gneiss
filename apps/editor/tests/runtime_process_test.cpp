// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

struct temporary_project final {
  std::filesystem::path root;

  ~temporary_project() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};

} // namespace

int main() try {
  gneiss::editor::runtime_process process;
  gneiss::editor::runtime_launch_request request{std::filesystem::path{GNEISS_TEST_PROJECT_ROOT}};
  const std::filesystem::path executable{GNEISS_TEST_RUNTIME};
  const auto missing_executable = executable.parent_path() / "missing-runtime";
  if (process.start(missing_executable, request) != gneiss::result::not_found) {
    return 1;
  }

  temporary_project invalid_project{std::filesystem::temp_directory_path() / "Gneiss" /
                                    "runtime-process-invalid-project"};
  std::error_code error;
  std::filesystem::remove_all(invalid_project.root, error);
  std::filesystem::create_directories(invalid_project.root / "assets", error);
  std::ofstream project_file(invalid_project.root / "gneiss.project.json",
                             std::ios::binary | std::ios::trunc);
  project_file << R"({
  "format": "gneiss.project",
  "version": 1,
  "name": "Invalid Runtime Project",
  "asset_root": "assets",
  "startup_scene": "asset://scenes/missing.scene.json"
})";
  project_file.close();
  if (error || !project_file) {
    return 2;
  }
  gneiss::editor::runtime_launch_request invalid_request{invalid_project.root};
  if (process.start(executable, invalid_request) != gneiss::result::success) {
    return 3;
  }
  const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (process.is_running() && std::chrono::steady_clock::now() < failure_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  const auto failure_log = process.log_file();
  if (process.is_running() || process.exit_code() != 2 ||
      process.output().find("stage=startup_scene") == std::string::npos ||
      process.output().find("missing.scene.json") == std::string::npos ||
      !std::filesystem::is_regular_file(failure_log)) {
    return 4;
  }

  if (process.start(executable, request) != gneiss::result::success || !process.is_running() ||
      process.start(executable, request) != gneiss::result::invalid_state) {
    return 5;
  }
  if (!std::filesystem::is_regular_file(failure_log)) {
    return 6;
  }

  const auto startup_started = std::chrono::steady_clock::now();
  const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < startup_deadline &&
         process.output().find("stage=first_frame") == std::string::npos) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!process.is_running() || process.output().find("stage=first_frame") == std::string::npos ||
      process.request_stop() != gneiss::result::success) {
    return 7;
  }
  const auto startup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - startup_started)
                              .count();
  std::printf("runtime_startup_to_first_frame_ms=%lld\n", static_cast<long long>(startup_ms));

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
    return 8;
  }

  if (process.start(GNEISS_TEST_CHILD_PROCESS, request) != gneiss::result::success ||
      process.request_stop() != gneiss::result::success) {
    return 9;
  }
  const auto forced_stop_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  while (process.is_running() && std::chrono::steady_clock::now() < forced_stop_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  if (process.is_running() || process.exit_code() == 0 ||
      process.output().find("已强制终止") == std::string::npos) {
    return 10;
  }
  return 0;
} catch (...) {
  return 99;
}
