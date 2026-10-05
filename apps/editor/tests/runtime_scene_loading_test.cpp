// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_process.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <stdexcept>
#include <thread>

namespace {
using namespace gneiss;
void check(bool value, std::source_location where = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("Runtime 场景加载契约失败，行=" + std::to_string(where.line()));
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
  const auto trace = project.root / "lifecycle.trace";
#if defined(_WIN32)
  (void)_putenv_s("GNEISS_GAME_MODULE_TEST_TRACE", trace.string().c_str());
  const auto module_name = "scene_game.dll";
#elif defined(__APPLE__)
  (void)setenv("GNEISS_GAME_MODULE_TEST_TRACE", trace.c_str(), 1);
  const auto module_name = "libscene_game.dylib";
#else
  (void)setenv("GNEISS_GAME_MODULE_TEST_TRACE", trace.c_str(), 1);
  const auto module_name = "libscene_game.so";
#endif
  std::filesystem::create_directories(project.root / "modules");
  std::filesystem::copy_file(GNEISS_TEST_SCENE_MODULE, project.root / "modules" / module_name);
  std::ofstream(project.root / "gneiss.project.json") << R"({"format":"gneiss.project","version":2,
    "name":"Scene loading test","asset_root":"assets","startup_scene":"asset://scenes/main.scene.json",
    "game_module":{"name":"scene_game","directory":"modules","configure_preset":"debug",
    "build_preset":"debug","build_target":"scene_game"}})";
  const auto lifecycle = [&] {
    std::ifstream stream(trace);
    std::string value;
    char event{};
    while (stream.get(event))
      if (event == 'I' || event == 'S')
        value += event;
    return value;
  };
  editor::runtime_process process;
  check(process.load_scene("asset://scenes/main.scene.json") == result::not_ready);
  check(process.start(GNEISS_TEST_RUNTIME, {project.root}) == result::success);
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
  check(process.scene_load_status().budget->upload_reserved_bytes == 0U);
  check(lifecycle() == "I");
  const auto old_session = process.scene_mirror().session_id();
  const auto old_node = process.scene_mirror().nodes().front().id;
  check(process.load_scene("asset://scenes/retry.scene.json") == result::success);
  await([&] { return process.scene_load_status().phase == ipc_scene_phase::failed; });
  check(process.is_running() && process.scene_mirror().session_id() == old_session &&
        process.scene_mirror().nodes().front().id == old_node);
  check(lifecycle() == "I");
  std::filesystem::copy_file(project.root / "assets/scenes/main.scene.json",
                             project.root / "assets/scenes/retry.scene.json");
  check(process.retry_scene_load() == result::success);
  await([&] {
    return process.scene_load_status().phase == ipc_scene_phase::applied &&
           !process.scene_mirror().nodes().empty() &&
           process.scene_mirror().session_id() != old_session;
  });
  const auto new_session = process.scene_mirror().session_id();
  check(lifecycle() == "ISI");
  check(process.load_scene("asset://scenes/main.scene.json") == result::success);
  check(process.cancel_scene_load() == result::success);
  await([&] { return process.scene_load_status().phase == ipc_scene_phase::cancelled; });
  check(process.scene_mirror().session_id() == new_session);
  check(lifecycle() == "ISI");
  check(process.retry_scene_load() == result::success);
  await([&] {
    return process.scene_load_status().phase == ipc_scene_phase::applied &&
           process.scene_mirror().session_id() != 0U &&
           process.scene_mirror().session_id() != new_session;
  });
  check(process.request_stop() == result::success);
  await([&] { return !process.is_running(); });
  check(process.exit_code() == 0 && process.received_shutdown_complete());
  check(lifecycle() == "ISISIS");
  // 首次加载失败保持空活动域，IPC 仍允许修复后的重试和正常退出。
  std::ofstream(project.root / "assets/scenes/main.scene.json") << "broken";
  check(process.start(GNEISS_TEST_RUNTIME, {project.root}) == result::success);
  await([&] { return process.scene_load_status().phase == ipc_scene_phase::failed; });
  check(process.is_running() && process.scene_mirror().nodes().empty());
  check(lifecycle() == "ISISIS");
  std::filesystem::copy_file(project.root / "assets/scenes/retry.scene.json",
                             project.root / "assets/scenes/main.scene.json",
                             std::filesystem::copy_options::overwrite_existing);
  check(process.retry_scene_load() == result::success);
  await([&] {
    return process.scene_load_status().phase == ipc_scene_phase::applied &&
           !process.scene_mirror().nodes().empty();
  });
  check(lifecycle() == "ISISISI");
  check(process.request_stop() == result::success);
  await([&] { return !process.is_running(); });
  check(process.exit_code() == 0 && process.received_shutdown_complete());
  check(lifecycle() == "ISISISIS");
}
}
int main() try {
  run();
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "%s\n", error.what());
  return 1;
}
