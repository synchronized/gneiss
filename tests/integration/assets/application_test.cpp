// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#ifdef GNEISS_TEST_NATIVE_WINDOW
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <stdexcept>
#include <thread>
namespace {
void check(bool value, const char* message) {
  if (!value)
    throw std::runtime_error(message);
}
#ifdef GNEISS_TEST_NATIVE_WINDOW
void close_pending_window(const char* assets) {
  using namespace gneiss;
  using namespace std::chrono_literals;
  gneiss::engine::task_scheduler scheduler({.workers = 1});
  runtime::application_configuration config;
  config.asset_root = assets;
  config.enable_window = true;
  config.window.visible = false;
  config.window.title = "Gneiss Asset pending close test";
  unsigned updates = 0;
  config.callbacks.update = [&](const auto&) {
    ++updates;
    return gneiss::engine::result::success;
  };
  std::shared_ptr<runtime::application> app;
  check(runtime::application::create(config, app) == gneiss::engine::result::success,
        "window create");
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success, "window attach");
  std::promise<void> release, entered;
  auto future = release.get_future().share();
  auto started = entered.get_future();
  const auto scope = scheduler.make_scope();
  // 异常时也先放行工作线程，再销毁 Application，避免测试自身制造关闭死锁。
  struct unblock {
    std::promise<void>& release;
    ~unblock() { release.set_value(); }
  };
  {
    unblock guard{release};
    gneiss::engine::task_handle handle;
    check(scheduler.submit(
              {.name = "test-input-gate", .scope = scope},
              [future, &entered](const auto&) {
                entered.set_value();
                future.wait();
                return gneiss::engine::task_outcome{};
              },
              handle) == gneiss::engine::submit_result::success,
          "gate submit");
    check(started.wait_for(5s) == std::future_status::ready, "gate started");
    check(app->begin_action_map("asset://input/default.input-map.json") ==
              gneiss::engine::result::success,
          "pending window input");
    check(app->action_map_status() == gneiss::engine::result::not_ready, "input remains pending");
    const auto window = FindWindowW(nullptr, L"Gneiss Asset pending close test");
    check(window != nullptr && PostMessageW(window, WM_CLOSE, 0, 0), "post native close");
    check(app->run(1) == gneiss::engine::result::success && updates == 0,
          "close handled before game update");
    check(app->action_map_status() == gneiss::engine::result::not_ready,
          "close did not wait for input");
  }
  check(app->shutdown() == gneiss::engine::result::success, "close drains input");
  check(scheduler.close_scope(scope), "close test scope");
  const auto stats = scheduler.stats();
  check(stats.waiting == 0 && stats.running == 0 && stats.retained == 0, "no remaining tasks");
}
#endif
void run(const char* assets, bool cooperative) {
  using namespace gneiss;
  using namespace std::chrono_literals;
  gneiss::engine::task_scheduler scheduler(
      {.workers = 1,
       .mode = cooperative ? gneiss::engine::execution_mode::cooperative
                           : gneiss::engine::execution_mode::thread_pool});
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-asset-application-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "input");
  const auto file = root / "input/default.input-map.json";
  std::filesystem::copy_file(std::filesystem::path(assets) / "input/default.input-map.json", file);
  struct cleanup {
    std::filesystem::path path;
    ~cleanup() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  } guard{root};
  const auto root_text = root.generic_string();
  runtime::application_configuration config;
  config.asset_root = root_text;
  std::shared_ptr<runtime::application> app;
  unsigned frames = 0;
  bool seen_ready = false;
  config.callbacks.update = [&](const auto&) {
    ++frames;
    if (app->action_map_status() == gneiss::engine::result::success) {
      gneiss::engine::action_id id;
      check(app->find_action("move_horizontal", id) == gneiss::engine::result::success,
            "ready before update");
      seen_ready = true;
    }
    return gneiss::engine::result::success;
  };
  check(runtime::application::create(config, app) == gneiss::engine::result::success, "create");
  check(app->begin_action_map("asset://input/default.input-map.json") ==
            gneiss::engine::result::not_ready,
        "requires executor");
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success, "attach");
#if !GNEISS_TEST_TASK_THREADS && !defined(__EMSCRIPTEN__)
  // 未配置真实异步后端的平台不能恢复旧同步回退；浏览器内存后端则验证完整加载流程。
  check(app->begin_action_map("asset://input/default.input-map.json").ok(),
        "unsupported backend request");
  for (unsigned i = 0; i < 8 && app->action_map_status() == engine::result::not_ready; ++i)
    check(app->run(1).ok(), "unsupported backend completion");
  check(app->action_map_status() == engine::result::unsupported, "no synchronous async fallback");
  check(app->shutdown().ok(), "unsupported backend shutdown");
  return;
#endif
  const auto wait = [&] {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    do {
      if (cooperative)
        check(scheduler.run_ready().status == gneiss::engine::drive_status::success, "drive");
      check(app->run(1) == gneiss::engine::result::success, "run");
      check(std::chrono::steady_clock::now() < deadline, "timeout");
#if GNEISS_TEST_TASK_THREADS
      std::this_thread::sleep_for(1ms);
#endif
    } while (app->action_map_status() == gneiss::engine::result::not_ready);
  };
  const auto close = [&] {
    check(app->request_shutdown().ok(), "request staged shutdown");
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    for (;;) {
      if (cooperative)
        check(scheduler.run_ready().status == engine::drive_status::success, "close drive");
      const auto status = app->advance_shutdown();
      if (status != engine::result::not_ready) {
        check(status.ok(), "staged shutdown complete");
        break;
      }
      check(std::chrono::steady_clock::now() < deadline, "close timeout");
#if GNEISS_TEST_TASK_THREADS
      std::this_thread::sleep_for(1ms);
#endif
    }
  };
  check(app->begin_action_map("asset://input/default.input-map.json") ==
            gneiss::engine::result::success,
        "begin");
  check(app->action_map_status() == gneiss::engine::result::not_ready, "admission only");
  wait();
  check(app->action_map_status() == gneiss::engine::result::success && frames > 0 && seen_ready,
        "applied");
  {
    std::ofstream oversized(root / "input/oversized.json", std::ios::binary);
    oversized << std::string(gneiss::engine::maximum_action_map_bytes + 1, ' ');
  }
  check(app->begin_action_map("asset://input/oversized.json") == gneiss::engine::result::success,
        "async oversized request admitted");
  wait();
  check(app->action_map_status() == gneiss::engine::result::invalid_argument,
        "async document limit");
  gneiss::engine::action_id original;
  check(app->find_action("move_horizontal", original) == gneiss::engine::result::success,
        "original action");
  {
    std::ofstream invalid(file);
    invalid << "{}";
  }
  check(app->begin_action_map("asset://input/default.input-map.json") ==
            gneiss::engine::result::success,
        "reload");
  wait();
  check(app->action_map_status() != gneiss::engine::result::success, "invalid reload failed");
  gneiss::engine::action_id retained;
  check(app->find_action("move_horizontal", retained) == gneiss::engine::result::success &&
            retained == original,
        "failure preserves action identity");
  check(app->begin_action_map("asset://input/missing.json") == gneiss::engine::result::success,
        "cancel begin");
  check(app->cancel_action_map() == gneiss::engine::result::success, "cancel");
  wait();
  check(app->action_map_status() == gneiss::engine::result::invalid_state,
        "cancel stays cancelled");
  std::filesystem::copy_file(std::filesystem::path(assets) / "input/default.input-map.json", file,
                             std::filesystem::copy_options::overwrite_existing);
  check(app->begin_action_map("asset://input/default.input-map.json") ==
            gneiss::engine::result::success,
        "begin before replacement");
  check(app->begin_action_map("asset://input/default.input-map.json").ok(),
        "new request replaces observer");
  wait();
  check(app->action_map_status().ok(), "new async request applied");
#if GNEISS_TEST_TASK_THREADS
  gneiss::engine::result cross_thread = gneiss::engine::result::success;
  std::thread other(
      [&] { cross_thread = app->begin_action_map("asset://input/default.input-map.json"); });
  other.join();
  check(cross_thread == gneiss::engine::result::invalid_state, "owner thread enforced");
#endif
  check(app->begin_action_map("asset://input/default.input-map.json") ==
            gneiss::engine::result::success,
        "shutdown begin");
  close();
  check(app->begin_action_map("asset://input/default.input-map.json") ==
            gneiss::engine::result::invalid_state,
        "shutdown rejects requests");
  // 资产管理器关闭只排空自身 scope，不能关闭借用的宿主调度器。
  app.reset();
  check(runtime::application::create(config, app) == gneiss::engine::result::success,
        "recreate application");
  check(app->attach_task_executor(scheduler) == gneiss::engine::result::success, "reuse executor");
  check(app->begin_action_map("asset://input/default.input-map.json") ==
            gneiss::engine::result::success,
        "new owner begins input");
  wait();
  check(app->action_map_status() == gneiss::engine::result::success,
        "new owner independently applies input");
  close();
}
}
int main(int argc, char** argv) {
  try {
    if (argc != 2)
      return 2;
    const auto* root = argv[1];
#if GNEISS_TEST_TASK_THREADS
    run(root, false);
#endif
    run(root, true);
#ifdef GNEISS_TEST_NATIVE_WINDOW
    close_pending_window(argv[1]);
#endif
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
