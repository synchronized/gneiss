// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_input.hpp"
#include "../../support/value_result.hpp"
#include <cstdio>
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/input/service.hpp>
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
void startup_systems() {
  for (unsigned scenario = 0; scenario != 3; ++scenario) {
    auto created = app::application::create({});
    check(created.ok());
    auto application = std::move(*created);
    std::string order;
    enum class state { ready };
    application->init_state(state::ready);
    application->on_start("first", [&] { order += 'a'; });
    application->on_start("second", [&]() -> error {
      order += 'b';
      return scenario == 1 ? error::io : error::success;
    });
    application->on_enter(state::ready, "enter", [&] { order += 'e'; });
    application->add_system(app::stage::update, "update", [&] {
      order += 'u';
      if (order == "abeuu")
        application->request_exit();
    });
    application->on_stop("release", [&] { order += 's'; });
    if (scenario == 2)
      application->request_exit();
    auto ran = application->run();
    check(scenario == 1 ? ran.error() == error::io : ran.ok());
    check(order == (scenario == 0 ? "abeuus" : scenario == 1 ? "abs" : "s"));
    // 终态观察不重放启动或清理。
    (void)application->step();
    check(order == (scenario == 0 ? "abeuus" : scenario == 1 ? "abs" : "s"));
  }
}

void scheduled_systems() {
  app::configuration config;
  std::string calls;
  auto created = app::application::create(config);
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
  check(application->step().ok() && application->step().ok());
  check(calls == "abspabsp");
  fail = true;
  calls.clear();
  check(application->run().error() == error::not_found);
  check(calls == "ab");
  // 失败进入终态，保留首次错误；不能重新执行系统。
  fail = false;
  calls.clear();
  check(application->step().error() == error::not_found);
  check(application->run().error() == error::not_found);
  check(calls.empty());
}
enum class flow { loading, playing, paused };
enum class overlay { hidden, visible };
void state_systems() {
  auto created = app::application::create({});
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
  check(application->step().ok() && calls == "Ll");
  check(application->step().ok() && calls == "LlxPp");
  // Enter 中请求其他状态类型也不会在同一边界递归执行。
  check(application->state<overlay>() == overlay::hidden);
  check(application->step().ok() && calls == "LlxPpAVa");
  check(application->request_state(flow::paused).ok());
  check(application->step().ok() && calls == "LlxPpAVaa");

  auto failing = app::application::create({});
  check(failing.ok());
  unsigned enters{};
  (*failing)->init_state(flow::loading);
  (*failing)->on_enter(flow::loading, "failure", [&] {
    ++enters;
    return error::io;
  });
  check((*failing)->run().error() == error::io);
  check((*failing)->run().error() == error::io && enters == 1);

  auto exiting = app::application::create({});
  check(exiting.ok());
  (*exiting)->init_state(flow::loading);
  (*exiting)->on_enter(flow::loading, "exit", [&] {
    (*exiting)->request_exit();
    check((*exiting)->request_state(flow::playing).error() == error::cancelled);
  });
  unsigned unexpected_updates{};
  (*exiting)->on_update(flow::loading, "must_not_run", [&] { ++unexpected_updates; });
  check((*exiting)->run().ok() && unexpected_updates == 0);
}
void host_shutdown() {
  std::string order;
  app::configuration config;
  auto created = app::application::create(config);
  check(created.ok());
  auto application = std::move(*created);
  auto* borrowed = application.get();
  application->on_stop("release", [&] {
    check(borrowed->step().error() == error::invalid_state);
    order += 'a';
    return error::io;
  });
  application->on_stop("release_remaining", [&]() -> result<void> {
    order += 'b';
    return {};
  });
  application->request_exit();
  auto stopping = application->step();
  check(stopping && *stopping == app::lifecycle::stopping && order == "ab");
  application->request_exit();
  check(application->run().error() == error::io && order == "ab");
  check(application->step().error() == error::io && order == "ab");
  application.reset();
  check(order == "ab");

  // 未运行就退出（例如宿主装配失败）也清理；析构保底不会重复显式关闭。
  created = app::application::create(config);
  check(created.ok());
  application = std::move(*created);
  application->on_stop("never_ran", [&] { order += 'c'; });
  application->request_exit();
  check(application->run().ok());
  application.reset();
  check(order == "abc");
  created = app::application::create(config);
  check(created.ok());
  application = std::move(*created);
  application->on_stop("destructor", [&] { order += 'd'; });
  application.reset();
  check(order == "abcd");
}
void complete_lifecycle() {
  auto created = app::application::create({});
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
  status = application->step();
  check(status && *status == app::lifecycle::stopped && releases == 1);

  created = app::application::create({});
  check(created.ok());
  application = std::move(*created);
  application->add_system(app::stage::update, "failure", [] { return error::io; });
  application->on_stop("cleanup", [&] { ++releases; });
  check(application->run().error() == error::io);
  check(releases == 2);
}
void simulation_pause() {
  enum class mode { playing, paused };
  std::uint64_t simulated{}, observed{}, latest_delta{};
  unsigned completions{}, updates{}, presented{};
  app::configuration config;
  auto created = app::application::create(config);
  check(created.ok());
  auto application = std::move(*created);
  application->init_state(mode::playing);
  application->add_system(app::stage::completions, "tasks", [&] { ++completions; });
  application->on_update(mode::playing, "simulation",
                         [&](const frame_time& time) { simulated += time.delta_ns; });
  application->add_system(app::stage::update, "editor", [&](const frame_time& time) {
    ++updates;
    latest_delta = time.delta_ns;
    check(time.elapsed_ns == observed + time.delta_ns);
    observed = time.elapsed_ns;
  });
  application->add_system(app::stage::present, "view", [&] { ++presented; });
  check(application->step().ok());
  const auto before_pause = simulated;
  check(application->request_state(mode::paused).ok());
  check(application->step().ok());
  check(application->step().ok());
  check(simulated == before_pause && updates == 3 && presented == 3 && completions == 3);
  check(application->request_state(mode::playing).ok());
  check(application->step().ok());
  // 恢复仅消费本帧时间，不补算暂停期间的应用时间。
  check(simulated == before_pause + latest_delta);
  application->request_exit();
  check(application->run().ok());
}
void starting_completions() {
  // 等待期间继续分发完成；成功、失败和主动退出都不得重放已完成步骤。
  for (unsigned mode = 0; mode < 3; ++mode) {
    auto created = app::application::create({});
    check(created.ok());
    auto application = std::move(*created);
    unsigned requests{}, polls{}, completions{}, updates{}, cleanup{}, entered{};
    application->on_start("request", [&] { ++requests; });
    application->on_start("wait", [&]() -> result<app::start_progress> {
      ++polls;
      if (completions < 3)
        return app::start_progress::pending;
      if (mode == 1)
        return error::not_found;
      return app::start_progress::complete;
    });
    application->on_start("ready", [&] { ++entered; });
    unsigned events{};
    application->add_system(app::stage::events, "events", [&](const app::event_context& input) {
      check(!input.close_requested());
      ++events;
    });
    application->add_system(app::stage::completions, "completion", [&] { ++completions; });
    application->add_system(app::stage::update, "update", [&] { ++updates; });
    application->on_stop("cleanup", [&] { ++cleanup; });
    auto first = application->step();
    check(first && *first == app::lifecycle::starting);
    check(requests == 1 && polls == 1 && updates == 0 && entered == 0);
    if (mode == 2) {
      application->request_exit();
    } else {
      auto second = application->step();
      check(second && *second == app::lifecycle::starting);
      auto third = application->step();
      if (mode == 0) {
        check(third && *third == app::lifecycle::running);
        check(entered == 1 && updates == 1);
        application->request_exit();
      }
    }
    check(events > 0);
    auto stopped = application->run();
    check(mode == 1 ? !stopped && stopped.error() == error::not_found : stopped.ok());
    check(requests == 1 && cleanup == 1);
    check(polls == (mode == 2 ? 1U : 3U));
    if (mode != 0)
      check(entered == 0 && updates == 0);
  }
}

void stopping_completions() {
  auto created = app::application::create({});
  check(created.ok());
  auto application = std::move(*created);
  unsigned dispatched{}, updates{}, cleanup{};
  application->add_system(app::stage::completions, "cooperative_tasks", [&] { ++dispatched; });
  application->add_system(app::stage::update, "request_stop", [&] {
    ++updates;
    application->request_exit();
  });
  application->on_stop("await_tasks", [&] {
    return dispatched >= 3 ? app::stop_progress::complete : app::stop_progress::pending;
  });
  application->on_stop("cleanup", [&] { ++cleanup; });
  check(application->run().ok());
  check(dispatched == 3 && updates == 1 && cleanup == 1);
}

void action_maps() {
  gneiss::tasks::scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  app::configuration config;
  config.asset_root = GNEISS_TEST_ASSET_ROOT;
  config.executor = &scheduler;
  std::shared_ptr<app::application> app, other;
  check(gneiss::test_support::assign_value(app::application::create(config), app).ok());
  constexpr auto uri = "asset://input/default.input-map.json";
  gneiss::input::action_id action;
  gneiss::input::action_state state;
  check(gneiss::test_support::load_input_for_test(*app, scheduler, uri).ok());
  check(app->input().find_action("move_horizontal", action).ok() && action.value != 0);
  check(app->input().get_action_state(action, state).ok() && !state.held && state.value == 0);
  check(gneiss::test_support::load_input_for_test(
            *app, scheduler, "asset://input/missing.input-map.json") == gneiss::error::not_found);
  check(app->input().get_action_state(action, state).ok());
  check(gneiss::test_support::load_input_for_test(*app, scheduler, uri).ok());
  check(app->input().get_action_state(action, state) == gneiss::error::invalid_handle);
  // 使用当前仍有效的动作验证跨 Application 拒绝，避免只验证旧代次。
  check(app->input().find_action("move_horizontal", action).ok());
  check(gneiss::test_support::assign_value(app::application::create(config), other).ok());
  check(gneiss::test_support::load_input_for_test(*other, scheduler, uri).ok());
  check(other->input().get_action_state(action, state) == gneiss::error::invalid_handle);
}
void creation_failures() {
  gneiss::tasks::scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  app::configuration config;
  std::shared_ptr<app::application> app;
  config.enable_window = true;
  config.window.width = 0;
  check(gneiss::test_support::assign_value(app::application::create(config), app) ==
        gneiss::error::invalid_argument);
  // 配置校验在创建对象前返回结果。
  check(!app);
  config.enable_window = false;
  config.window.width = 1280;
  config.asset_root = "gneiss-test-directory-does-not-exist";
  config.executor = &scheduler;
  // 挂载只配置来源；缺失目录在实际异步读取时报告，不在创建阶段同步探测。
  check(gneiss::test_support::assign_value(app::application::create(config), app).ok());
  check(test_support::load_input_for_test(
            *app, scheduler, "asset://input/missing.input-map.json") == gneiss::error::not_found);
  app.reset();
  config.asset_root = {};
  check(gneiss::test_support::assign_value(app::application::create(config), app).ok());
  check(!app->async_files());
  app->on_start("fail", [] { return gneiss::error::not_found; });
  check(app->run().error() == gneiss::error::not_found);
  check(app->step().error() == gneiss::error::not_found);
}
}
int main() try {
  startup_systems();
  scheduled_systems();
  state_systems();
  host_shutdown();
  complete_lifecycle();
  starting_completions();
  stopping_completions();
  simulation_pause();
  action_maps();
  creation_failures();
  return 0;
} catch (...) {
  return 1;
}
