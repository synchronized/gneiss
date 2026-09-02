// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.h"

#include <algorithm>
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
         (process.output().find("Runtime 已进入首帧") == std::string::npos ||
          process.control_state() != gneiss::editor::runtime_control_state::running)) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  const auto current_session = process.console().current_session_id();
  const auto has_structured_event = [&] {
    return std::ranges::any_of(process.console().entries(), [current_session](const auto& entry) {
      return entry.session_id == current_session &&
             entry.kind == gneiss::editor::console_entry_kind::structured;
    });
  };
  while (std::chrono::steady_clock::now() < startup_deadline && !has_structured_event()) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!process.is_running() || process.output().find("Runtime 已进入首帧") == std::string::npos ||
      !has_structured_event() ||
      process.control_state() != gneiss::editor::runtime_control_state::running ||
      process.request_pause() != gneiss::result::success) {
    return 7;
  }
  const auto lifecycle_event_count =
      std::ranges::count_if(process.console().entries(), [current_session](const auto& entry) {
        return entry.session_id == current_session &&
               entry.kind == gneiss::editor::console_entry_kind::structured &&
               entry.event.category == "lifecycle" && entry.event.message == "Runtime 已进入首帧";
      });
  if (lifecycle_event_count != 1) {
    return 7;
  }
  const auto pause_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (process.control_state() != gneiss::editor::runtime_control_state::paused &&
         std::chrono::steady_clock::now() < pause_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (process.control_state() != gneiss::editor::runtime_control_state::paused ||
      process.request_resume() != gneiss::result::success) {
    return 7;
  }
  const auto resume_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (process.control_state() != gneiss::editor::runtime_control_state::running &&
         std::chrono::steady_clock::now() < resume_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (process.control_state() != gneiss::editor::runtime_control_state::running ||
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
  if (process.is_running() || process.exit_code() != 0 || !process.received_shutdown_complete() ||
      process.output().find("收到 Editor IPC 停止请求") == std::string::npos ||
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

  gneiss::app::project_description module_project;
  module_project.project_root = request.project_root;
  module_project.game_module.name = "test_game";
  module_project.game_module.directory = "modules";
  module_project.game_module.build_preset = "game-debug";
  module_project.game_module.build_target = "build-fail";
  if (process.build_and_start(GNEISS_TEST_CHILD_PROCESS, executable, request, module_project) !=
          gneiss::result::success ||
      !process.is_building()) {
    return 11;
  }
  const auto build_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (process.is_building() && std::chrono::steady_clock::now() < build_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  if (process.is_busy() || process.last_result() != gneiss::result::dependency_failed ||
      process.output().find("fixture build failed") == std::string::npos ||
      process.output().find("未启动 Runtime") == std::string::npos) {
    return 12;
  }

  std::filesystem::remove_all(invalid_project.root / "assets", error);
  std::filesystem::copy(std::filesystem::path{GNEISS_TEST_PROJECT_ROOT} / "assets",
                        invalid_project.root / "assets", std::filesystem::copy_options::recursive,
                        error);
  project_file.open(invalid_project.root / "gneiss.project.json",
                    std::ios::binary | std::ios::trunc);
  project_file << R"({
  "format": "gneiss.project",
  "version": 1,
  "name": "Build Success Project",
  "asset_root": "assets",
  "startup_scene": "asset://scenes/main.scene.json"
})";
  project_file.close();
  module_project.project_root = invalid_project.root;
  module_project.game_module.build_target = "build-success";
  invalid_request.project_root = invalid_project.root;
  if (error || !project_file ||
      process.build_and_start(GNEISS_TEST_CHILD_PROCESS, executable, invalid_request,
                              module_project) != gneiss::result::success) {
    return 13;
  }
  const auto build_success_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < build_success_deadline &&
         process.output().find("Runtime 已进入首帧") == std::string::npos && process.is_busy()) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!process.is_running() || process.output().find("游戏模块构建完成") == std::string::npos ||
      process.output().find("Runtime 已进入首帧") == std::string::npos ||
      process.request_stop() != gneiss::result::success) {
    return 14;
  }
  return 0;
} catch (...) {
  return 99;
}
