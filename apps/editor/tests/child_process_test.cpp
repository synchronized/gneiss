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
  missing.executable =
      std::filesystem::path{GNEISS_TEST_CHILD_PROCESS}.parent_path() / "missing-process";
  if (process.start(missing) != gneiss::result::not_found || process.is_running()) {
    return 1;
  }

  gneiss::child_process_start_info info;
  info.executable = GNEISS_TEST_CHILD_PROCESS;
  info.arguments = {"exit"};
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
  std::string incremental_output;
  process.consume_output(incremental_output);
  if (process.is_running() || process.exit_code() != 23 ||
      process.output().find("fixture stdout") == std::string::npos ||
      process.output().find("fixture stderr") == std::string::npos ||
      incremental_output.find("fixture stdout") == std::string::npos ||
      incremental_output.find("fixture stderr") == std::string::npos ||
      process.terminate() != gneiss::result::not_ready) {
    return 3;
  }

  info.arguments = {"wait"};
  if (process.start(info) != gneiss::result::success) {
    return 4;
  }
  const auto output_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (process.output().find("fixture waiting") == std::string::npos &&
         std::chrono::steady_clock::now() < output_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!process.is_running() || process.terminate() != gneiss::result::success) {
    return 5;
  }
  const auto terminate_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (process.is_running() && std::chrono::steady_clock::now() < terminate_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (process.is_running() || process.exit_code() == 0) {
    return 6;
  }

  info.arguments = {"exit"};
  if (process.start(info) != gneiss::result::success) {
    return 7;
  }
  const auto relaunch_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (process.is_running() && std::chrono::steady_clock::now() < relaunch_deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  process.update();
  if (process.is_running() || process.exit_code() != 23) {
    return 8;
  }

  const auto cleanup_started = std::chrono::steady_clock::now();
  {
    gneiss::child_process abandoned;
    info.arguments = {"wait"};
    if (abandoned.start(info) != gneiss::result::success) {
      return 9;
    }
  }
  if (std::chrono::steady_clock::now() - cleanup_started > std::chrono::seconds(3)) {
    return 10;
  }
  return 0;
} catch (...) {
  return 99;
}
