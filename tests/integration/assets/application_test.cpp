// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../support/value_result.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/resource/environment.hpp>
#include <gneiss/engine/resource/input/action_map.hpp>
#ifdef GNEISS_TEST_NATIVE_WINDOW
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <stdexcept>
#include <string_view>
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
  gneiss::tasks::task_scheduler scheduler({.workers = 1});
  runtime::application_configuration config;
  config.asset_root = assets;
  config.executor = &scheduler;
  config.enable_window = true;
  config.window.visible = false;
  config.window.title = "Gneiss Asset pending close test";
  unsigned updates = 0;
  std::shared_ptr<runtime::application> app;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
            gneiss::error::success,
        "window create");

  app->add_system(gneiss::app::stage::update, "test.update", [&](const auto&) {
    ++updates;
    return gneiss::error::success;
  });
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
    gneiss::tasks::task_handle handle;
    check(scheduler.submit(
              {.name = "test-input-gate", .scope = scope},
              [future, &entered](const auto&) {
                entered.set_value();
                future.wait();
                return gneiss::tasks::task_outcome{};
              },
              handle) == gneiss::tasks::submit_result::success,
          "gate submit");
    check(started.wait_for(5s) == std::future_status::ready, "gate started");
    auto request = app->assets().reload<input::action_map>("asset://input/default.input-map.json");
    check(request.ok(), "pending window input");
    check(request->status() == gneiss::error::not_ready, "input remains pending");
    auto environment =
        app->assets().load<gneiss::environment>("asset://input/default.input-map.json");
    check(environment && environment->status() == error::not_ready, "environment remains pending");
    environment->cancel();
    check(!*environment, "host cancels environment before close");
    const auto window = FindWindowW(nullptr, L"Gneiss Asset pending close test");
    check(window != nullptr && PostMessageW(window, WM_CLOSE, 0, 0), "post native close");
    check(app->step().ok() && updates == 0, "close handled before game update");
    check(!app->accepts_requests(), "close rejects new input requests");
  }
  app->request_exit();
  check(app->run().ok(), "close drains input");
  check(scheduler.close_scope(scope), "close test scope");
  const auto stats = scheduler.stats();
  check(stats.waiting == 0 && stats.running == 0 && stats.retained == 0, "no remaining tasks");
}
#endif
void run(const char* assets, bool cooperative) {
  using namespace gneiss;
  using namespace std::chrono_literals;
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 1,
       .mode = cooperative ? gneiss::tasks::execution_mode::cooperative
                           : gneiss::tasks::execution_mode::thread_pool});
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
  config.executor = &scheduler;
  std::shared_ptr<runtime::application> app;
  unsigned frames = 0;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
            gneiss::error::success,
        "create");

  app->add_system(gneiss::app::stage::update, "test.update", [&](const auto&) {
    ++frames;
    return gneiss::error::success;
  });
#if !GNEISS_TEST_TASK_THREADS && !defined(__EMSCRIPTEN__)
  // 未配置真实异步后端的平台不能恢复旧同步回退；浏览器内存后端则验证完整加载流程。
  auto unsupported =
      app->assets().reload<input::action_map>("asset://input/default.input-map.json");
  check(unsupported.ok(), "unsupported backend request");
  for (unsigned i = 0; i < 8 && unsupported->status() == gneiss::error::not_ready; ++i)
    check(app->step().ok(), "unsupported backend completion");
  check(unsupported->status() == gneiss::error::unsupported, "no synchronous async fallback");
  app->request_exit();
  check(app->run().ok(), "unsupported backend shutdown");
  return;
#endif
  const auto wait = [&](const auto& request) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    do {
      if (cooperative)
        check(scheduler.run_ready().status == gneiss::tasks::drive_status::success, "drive");
      check(app->step().ok(), "run");
      check(std::chrono::steady_clock::now() < deadline, "timeout");
#if GNEISS_TEST_TASK_THREADS
      std::this_thread::sleep_for(1ms);
#endif
    } while (request.status() == gneiss::error::not_ready);
  };
  const auto close = [&] {
    app->request_exit();
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    for (;;) {
      if (cooperative)
        check(scheduler.run_ready().status == gneiss::tasks::drive_status::success, "close drive");
      const auto status = app->step();
      check(status.ok(), "staged shutdown progress");
      if (*status == gneiss::app::lifecycle::stopped)
        break;
      check(std::chrono::steady_clock::now() < deadline, "close timeout");
#if GNEISS_TEST_TASK_THREADS
      std::this_thread::sleep_for(1ms);
#endif
    }
  };
  const auto environment_file = root / "environment.bin";
  std::ofstream(environment_file, std::ios::binary) << "first";
  auto environment_request = app->assets().load<gneiss::environment>("asset://environment.bin");
  check(environment_request.ok(), "environment request");
  wait(*environment_request);
  auto environment = environment_request->take();
  check(environment.ok(), "environment loaded");
  auto snapshot = (*environment)->snapshot();
  check(snapshot && snapshot->size() == 5, "environment immutable bytes");
  std::ofstream(environment_file, std::ios::binary) << "second";
  auto reloaded = app->assets().reload<gneiss::environment>("asset://environment.bin");
  check(reloaded.ok(), "environment reload");
  wait(*reloaded);
  auto replacement = reloaded->take();
  check(replacement && *replacement == *environment && snapshot->size() == 5 &&
            (*environment)->snapshot()->size() == 6,
        "environment identity and old snapshot");
  std::ofstream(environment_file, std::ios::binary).close();
  auto empty_environment = app->assets().reload<gneiss::environment>("asset://environment.bin");
  check(empty_environment.ok(), "empty environment admitted");
  wait(*empty_environment);
  check(empty_environment->status() == error::invalid_argument &&
            (*environment)->snapshot()->size() == 6,
        "invalid environment preserves content");
  std::filesystem::resize_file(environment_file, 64U * 1024U * 1024U + 1U);
  auto oversized_environment = app->assets().reload<gneiss::environment>("asset://environment.bin");
  check(oversized_environment.ok(), "oversized environment admitted");
  wait(*oversized_environment);
  check(oversized_environment->status().failed() && (*environment)->snapshot()->size() == 6,
        "environment source limit");

  auto request = app->assets().reload<input::action_map>("asset://input/default.input-map.json");
  check(request.ok(), "begin");
  wait(*request);
  auto loaded = request->take();
  check(loaded.ok() && frames > 0, "loaded while application advances");
  input::action_id absent;
  check(app->input().find_action("move_horizontal", absent) != error::success,
        "loading does not implicitly apply input");
  check(app->input().replace_action_map(*(*loaded)->snapshot()).ok(), "host applies input");
  {
    std::ofstream oversized(root / "input/oversized.json", std::ios::binary);
    oversized << std::string(gneiss::input::maximum_action_map_bytes + 1, ' ');
  }
  auto oversized = app->assets().reload<input::action_map>("asset://input/oversized.json");
  check(oversized.ok(), "async oversized request admitted");
  wait(*oversized);
  check(oversized->status() == error::invalid_argument, "async document limit");
  gneiss::input::action_id original;
  check(app->input().find_action("move_horizontal", original) == gneiss::error::success,
        "original action");
  {
    std::ofstream invalid(file);
    invalid << "{}";
  }
  auto invalid = app->assets().reload<input::action_map>("asset://input/default.input-map.json");
  check(invalid.ok(), "reload");
  wait(*invalid);
  check(!invalid->take(), "invalid reload failed");
  gneiss::input::action_id retained;
  check(app->input().find_action("move_horizontal", retained) == gneiss::error::success &&
            retained == original,
        "failure preserves action identity");
  auto cancelled = app->assets().reload<input::action_map>("asset://input/missing.json");
  check(cancelled.ok(), "cancel begin");
  cancelled->cancel();
  check(!*cancelled, "host releases request");
  std::filesystem::copy_file(std::filesystem::path(assets) / "input/default.input-map.json", file,
                             std::filesystem::copy_options::overwrite_existing);
  auto pending = app->assets().reload<input::action_map>("asset://input/default.input-map.json");
  check(pending.ok(), "shutdown begin");
  close();
  check(!app->accepts_requests(), "shutdown rejects requests");
  // 资产管理器关闭只排空自身 scope，不能关闭借用的宿主调度器。
  app.reset();
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
            gneiss::error::success,
        "recreate application");

  auto reopened = app->assets().reload<input::action_map>("asset://input/default.input-map.json");
  check(reopened.ok(), "new owner begins input");
  wait(*reopened);
  auto reopened_map = reopened->take();
  check(reopened_map.ok(), "new owner independently loads input");
  check(app->input().replace_action_map(*(*reopened_map)->snapshot()).ok(),
        "new owner applies input");
  close();
}
}
int main(int argc, char** argv) {
  try {
#ifdef GNEISS_TEST_NATIVE_WINDOW
    if (argc == 3 && std::string_view{argv[2]} == "--native-window") {
      close_pending_window(argv[1]);
      return 0;
    }
#endif
    if (argc != 2)
      return 2;
    const auto* root = argv[1];
#if GNEISS_TEST_TASK_THREADS
    run(root, false);
#endif
    run(root, true);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
