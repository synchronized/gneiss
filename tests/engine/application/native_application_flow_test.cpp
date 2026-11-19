// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_input.hpp"
#include "../../support/value_result.hpp"
#include <cstdio>
#include <gneiss/engine/core/tasks/task_scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <source_location>
#include <stdexcept>
namespace {
using namespace gneiss;
void check(bool value, std::source_location location = std::source_location::current()) {
  if (!value) {
    std::fprintf(stderr, "Application 原生流程失败：%s:%u\n", location.file_name(),
                 location.line());
    throw std::runtime_error("Application 原生流程失败");
  }
}
void loop() {
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration config;
  std::uint64_t clock{}, updates{}, shutdowns{}, closes{};
  bool close{}, allow{};
  config.callbacks.now_ns = [&] { return clock += 10; };
  config.callbacks.poll_events = [&](bool& output) {
    output = close;
    return gneiss::error::success;
  };
  config.callbacks.close_requested = [&]() {
    ++closes;
    return allow;
  };
  config.callbacks.shutdown = [&] { ++shutdowns; };
  config.callbacks.update = [&](const gneiss::frame_time& time) {
    check(time.frame_index == updates && (updates == 0 || time.delta_ns == 10));
    gneiss::ui_draw_view ui;
    ui.display_width = 640;
    ui.display_height = 480;
    check(app->submit_ui_draw_list(ui).ok());
    check(app->submit_debug_draw_list({}).ok());
    if (++updates == 3)
      app->request_exit();
    return gneiss::error::success;
  };
  check(gneiss::test_support::assign_value(runtime::application::create(config), app).ok());
  std::uint32_t width{}, height{};
  check(app->get_window_size(width, height).ok() && width == 1280 && height == 720);
  gneiss::input::event event;
  check(app->poll_input(event) == gneiss::error::not_ready &&
        event.type == gneiss::input::event_type::none);
  check(app->keyboard_state().modifiers == 0 && app->keyboard_state().pressed_keys[0] == 0);
  check(app->pointer_state().buttons == 0 && !app->pointer_state().is_inside);
  check(app->run(8).ok() && updates == 3);
  app.reset();
  check(shutdowns == 1);
  close = allow = true;
  updates = closes = 0;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app).ok());
  check(app->run(8).ok() && updates == 0 && closes == 1);
  app.reset();
  allow = false;
  closes = 0;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app).ok());
  check(app->run(8).ok() && updates == 3 && closes == 3);
  app.reset();
  check(shutdowns == 3);
}
void scheduled_systems() {
  runtime::application_configuration config;
  std::string calls;
  unsigned diagnostics{};
  config.callbacks.diagnostic = [&](const diagnostic_view& event) {
    if (event.result == error::not_found)
      ++diagnostics;
  };
  auto created = runtime::application::create(config);
  check(created.ok());
  auto application = *created;
  // 故意跨阶段注册，验证阶段顺序以及同阶段的注册顺序。
  application->add_system(app::stage::present, "ui", [&] { calls += 'p'; });
  application->add_system(app::stage::update, "logic", [&](const frame_time&) { calls += 'a'; });
  application->add_system(app::stage::synchronize, "sync", [&]() -> result<void> {
    calls += 's';
    return {};
  });
  bool fail{};
  application->add_system(app::stage::update, "second", [&] {
    calls += 'b';
    return fail ? error::not_found : error::success;
  });
  check(application->run(2).ok());
  check(calls == "abspabsp");
  fail = true;
  calls.clear();
  check(application->run(1) == error::not_found);
  check(calls == "ab" && diagnostics == 1);
  // 失败退出不能留下 running/updating 标记，下一次运行仍能正常完成。
  fail = false;
  calls.clear();
  check(application->run(1).ok() && calls == "absp");
}
enum class flow { loading, playing, paused };
enum class overlay { hidden, visible };
void state_systems() {
  auto created = runtime::application::create({});
  check(created.ok());
  auto application = *created;
  std::string calls;
  application->init_state(flow::loading);
  application->init_state(overlay::hidden);
  application->on_enter(flow::loading, "begin", [&] { calls += 'L'; });
  application->on_update(flow::loading, "load", [&] {
    calls += 'l';
    check(application->request_state(flow::playing).ok());
    check(application->request_state(flow::playing).ok());
    check(application->request_state(flow::paused).error() == error::invalid_state);
    check(application->state<flow>() == flow::loading);
  });
  application->on_exit(flow::loading, "leave_loading", [&] { calls += 'x'; });
  application->on_enter(flow::playing, "play", [&] {
    calls += 'P';
    check(application->request_state(flow::paused).ok());
    check(application->request_state(overlay::visible).ok());
  });
  application->on_update(flow::playing, "playing", [&] { calls += 'p'; });
  application->on_enter(flow::paused, "pause", [&] { calls += 'A'; });
  application->on_update(flow::paused, "paused", [&] { calls += 'a'; });
  application->on_enter(overlay::visible, "show", [&] { calls += 'V'; });
  check(application->run(1).ok() && calls == "Ll");
  check(application->run(1).ok() && calls == "LlxPp");
  // Enter 中请求其他状态类型也不会在同一边界递归执行。
  check(application->state<overlay>() == overlay::hidden);
  check(application->run(1).ok() && calls == "LlxPpAVa");
  check(application->request_state(flow::paused).ok());
  check(application->run(1).ok() && calls == "LlxPpAVaa");

  auto failing = runtime::application::create({});
  check(failing.ok());
  unsigned enters{};
  (*failing)->init_state(flow::loading);
  (*failing)->on_enter(flow::loading, "failure", [&] {
    ++enters;
    return error::io;
  });
  check((*failing)->run(1) == error::io);
  check((*failing)->run(1) == error::io && enters == 1);

  auto exiting = runtime::application::create({});
  check(exiting.ok());
  (*exiting)->init_state(flow::loading);
  (*exiting)->on_enter(flow::loading, "exit", [&] {
    (*exiting)->request_exit();
    check((*exiting)->request_state(flow::playing).error() == error::cancelled);
  });
  (*exiting)->on_update(flow::loading, "must_not_run", [&] { check(false); });
  check((*exiting)->run(1).ok());
}
void host_shutdown() {
  std::string order;
  runtime::application_configuration config;
  config.callbacks.shutdown = [&] { order += 's'; };
  auto created = runtime::application::create(config);
  check(created.ok());
  auto application = std::move(*created);
  auto* borrowed = application.get();
  application->on_stop("release", [&] {
    check(borrowed->native_world() != nullptr);
    check(borrowed->run(1) == error::invalid_state);
    check(borrowed->request_shutdown() == error::invalid_state);
    order += 'a';
    return error::io;
  });
  application->on_stop("release_remaining", [&]() -> result<void> {
    check(borrowed->native_world() != nullptr);
    order += 'b';
    return {};
  });
  check(application->request_shutdown().ok());
  check(order == "ab");
  check(application->run(1) == error::invalid_state);
  check(application->request_shutdown().ok() && order == "ab");
  check(application->advance_shutdown() == error::io && order == "abs");
  check(application->native_world() == nullptr);
  check(application->advance_shutdown() == error::io);
  application.reset();
  check(order == "abs");

  // 未运行就退出（例如宿主装配失败）也清理；析构保底不会重复显式关闭。
  created = runtime::application::create(config);
  check(created.ok());
  application = std::move(*created);
  application->on_stop("never_ran", [&] { order += 'c'; });
  check(application->shutdown().ok());
  application.reset();
  check(order == "abscs");
  created = runtime::application::create(config);
  check(created.ok());
  application = std::move(*created);
  application->on_stop("destructor", [&] { order += 'd'; });
  application.reset();
  check(order == "abscsds");
}
void complete_lifecycle() {
  auto created = runtime::application::create({});
  check(created.ok());
  auto application = std::move(*created);
  unsigned updates{}, releases{}, waits{};
  bool finished{};
  application->add_system(app::stage::update, "step_update", [&] {
    check(application->step().error() == error::invalid_state);
    if (++updates == 2)
      application->request_exit();
  });
  application->on_stop("pending", [&] {
    ++waits;
    check(application->native_world() != nullptr);
    return finished ? app::stop_progress::complete : app::stop_progress::pending;
  });
  application->on_stop("release", [&] { ++releases; });
  auto first = application->step();
  check(first && *first == app::lifecycle::running);
  auto status = application->step();
  check(status && *status == app::lifecycle::stopping && updates == 2 && waits == 1);
  status = application->step();
  check(status && *status == app::lifecycle::stopping && releases == 0 && waits == 2);
  finished = true;
  status = application->step();
  check(status && *status == app::lifecycle::stopped && releases == 1);
  check(application->native_world() == nullptr);
  status = application->step();
  check(status && *status == app::lifecycle::stopped && releases == 1);

  created = runtime::application::create({});
  check(created.ok());
  application = std::move(*created);
  application->add_system(app::stage::update, "failure", [] { return error::io; });
  application->on_stop("cleanup", [&] { ++releases; });
  check(application->run().error() == error::io);
  check(releases == 2 && application->native_world() == nullptr);
}
void action_maps() {
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  runtime::application_configuration config;
  config.asset_root = GNEISS_TEST_ASSET_ROOT;
  std::shared_ptr<runtime::application> app, other;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app).ok());
  constexpr auto uri = "asset://input/default.input-map.json";
  gneiss::input::action_id action;
  gneiss::input::action_state state;
  check(gneiss::test_support::load_input_for_test(*app, scheduler, uri).ok());
  check(app->find_action("move_horizontal", action).ok() && action.value != 0);
  check(app->get_action_state(action, state).ok() && !state.held && state.value == 0);
  check(gneiss::test_support::load_input_for_test(
            *app, scheduler, "asset://input/missing.input-map.json") == gneiss::error::not_found);
  check(app->get_action_state(action, state).ok());
  check(gneiss::test_support::load_input_for_test(*app, scheduler, uri).ok());
  check(app->get_action_state(action, state) == gneiss::error::invalid_handle);
  // 使用当前仍有效的动作验证跨 Application 拒绝，避免只验证旧代次。
  check(app->find_action("move_horizontal", action).ok());
  check(gneiss::test_support::assign_value(runtime::application::create(config), other).ok());
  check(gneiss::test_support::load_input_for_test(*other, scheduler, uri).ok());
  check(other->get_action_state(action, state) == gneiss::error::invalid_handle);
}
void creation_failures() {
  runtime::application_configuration config;
  std::shared_ptr<runtime::application> app;
  unsigned diagnostics{};
  gneiss::error reported;
  config.callbacks.diagnostic = [&](const gneiss::diagnostic_view& event) {
    check(!event.message.empty());
    ++diagnostics;
    reported = event.result;
  };
  config.enable_window = true;
  config.window.width = 0;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::error::invalid_argument);
  // 配置校验在创建对象前返回结果，不经过运行期诊断回调。
  check(!app && diagnostics == 0);
  config.enable_window = false;
  config.window.width = 1280;
  config.asset_root = "gneiss-test-directory-does-not-exist";
  // 挂载只配置来源；缺失目录在实际异步读取时报告，不在创建阶段同步探测。
  check(gneiss::test_support::assign_value(runtime::application::create(config), app).ok());
  gneiss::tasks::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  check(test_support::load_input_for_test(
            *app, scheduler, "asset://input/missing.input-map.json") == gneiss::error::not_found);
  app.reset();
  config.asset_root = {};
  config.callbacks.initialize = [] { return gneiss::error::not_found; };
  diagnostics = 0;
  check(gneiss::test_support::assign_value(runtime::application::create(config), app) ==
        gneiss::error::not_found);
  check(!app && diagnostics == 1 && reported == gneiss::error::not_found);
}
}
int main() try {
  loop();
  scheduled_systems();
  state_systems();
  host_shutdown();
  complete_lifecycle();
  action_maps();
  creation_failures();
  return 0;
} catch (...) {
  return 1;
}
