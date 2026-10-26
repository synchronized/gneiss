// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "runtime_process.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <thread>

using namespace gneiss::host;

namespace {
using namespace gneiss;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    throw std::runtime_error("Runtime 场景加载契约失败，行=" + std::to_string(where.line()));
  }
}
struct fixture {
  std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("gneiss-scene-ipc-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fixture() {
    std::filesystem::create_directories(root);
    std::filesystem::copy(GNEISS_TEST_PROJECT_ROOT, root, std::filesystem::copy_options::recursive);
  }
  ~fixture() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};
void run() {
  fixture project;
  host::runtime_process process;
  check(process.load_scene("asset://scenes/main.scene.json") == gneiss::engine::result::not_ready);
  check(process.start(GNEISS_TEST_RUNTIME, {project.root}) == gneiss::engine::result::success);
  const auto await = [&](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        std::fprintf(stderr, "%s\n", process.output().c_str());
        throw std::runtime_error("等待场景 IPC 状态超时");
      }
      process.update();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  };
  await([&] {
    return process.scene_load_status().phase == ipc_scene_phase::applied &&
           !process.scene_mirror().nodes().empty();
  });
  check(process.supports_scene_loading());
  check(process.scene_load_status().budget.has_value());
  check(process.scene_load_status().budget->available_bytes > 0U);
  const auto old_session = process.scene_mirror().session_id();
  const auto old_node = process.scene_mirror().nodes().front().id;
  check(process.load_scene("asset://scenes/retry.scene.json") == gneiss::engine::result::success);
  await([&] { return process.scene_load_status().phase == ipc_scene_phase::failed; });
  check(process.is_running() && process.scene_mirror().session_id() == old_session &&
        process.scene_mirror().nodes().front().id == old_node);
  std::filesystem::copy_file(project.root / "assets/scenes/main.scene.json",
                             project.root / "assets/scenes/retry.scene.json");
  check(process.retry_scene_load() == gneiss::engine::result::success);
  await([&] {
    return process.scene_load_status().phase == ipc_scene_phase::applied &&
           !process.scene_mirror().nodes().empty() &&
           process.scene_mirror().session_id() != old_session;
  });
  const auto new_session = process.scene_mirror().session_id();
  check(process.load_scene("asset://scenes/main.scene.json") == gneiss::engine::result::success);
  check(process.cancel_scene_load() == gneiss::engine::result::success);
  await([&] { return process.scene_load_status().phase == ipc_scene_phase::cancelled; });
  check(process.scene_load_status().budget && process.scene_load_status().budget->cleanup_complete);
  check(process.scene_mirror().session_id() == new_session);
  check(process.retry_scene_load() == gneiss::engine::result::success);
  await([&] {
    return process.scene_load_status().phase == ipc_scene_phase::applied &&
           process.scene_mirror().session_id() != 0U &&
           process.scene_mirror().session_id() != new_session;
  });
  check(process.request_stop() == gneiss::engine::result::success);
  await([&] { return !process.is_running(); });
  check(process.exit_code() == 0 && process.received_shutdown_complete());
  // 首次加载失败保持空活动域，IPC 仍允许修复后的重试和正常退出。
  std::ofstream(project.root / "assets/scenes/main.scene.json") << "broken";
  check(process.start(GNEISS_TEST_RUNTIME, {project.root}) == gneiss::engine::result::success);
  await([&] { return process.scene_load_status().phase == ipc_scene_phase::failed; });
  check(process.is_running() && process.scene_mirror().nodes().empty());
  std::filesystem::copy_file(project.root / "assets/scenes/retry.scene.json",
                             project.root / "assets/scenes/main.scene.json",
                             std::filesystem::copy_options::overwrite_existing);
  check(process.retry_scene_load() == gneiss::engine::result::success);
  await([&] {
    return process.scene_load_status().phase == ipc_scene_phase::applied &&
           !process.scene_mirror().nodes().empty();
  });
  check(process.request_stop() == gneiss::engine::result::success);
  await([&] { return !process.is_running(); });
  check(process.exit_code() == 0 && process.received_shutdown_complete());
}
}
int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
