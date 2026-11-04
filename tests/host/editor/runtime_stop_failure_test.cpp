// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "runtime_process.hpp"
#include <chrono>
#include <filesystem>
#include <thread>
int main() try {
  gneiss::host::runtime_process process;
  const auto root = std::filesystem::temp_directory_path();
  if (process.start(GNEISS_TEST_CHILD_PROCESS, {.project_root = root}).failed())
    return 1;
  const auto session = process.log_file().parent_path();
  const auto stop = session / "stop.signal";
  // 只阻塞本用例进程的通知文件；无图形和场景加载。
  if (!std::filesystem::create_directory(stop))
    return 2;
  const auto status = process.request_stop();
  if (status != gneiss::engine::error::io)
    return 3;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (process.is_running() && std::chrono::steady_clock::now() < deadline) {
    process.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (process.is_running())
    return 4;
  std::error_code ignored;
  std::filesystem::remove(stop, ignored);
  std::filesystem::remove(session, ignored);
  return 0;
} catch (...) {
  return 99;
}
