// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime/application.hpp"
#include <stdexcept>
namespace {
using namespace gneiss;
void check(bool value) {
  if (!value)
    throw std::runtime_error("Application 原生流程失败");
}
void loop() {
  std::shared_ptr<runtime::application> app;
  runtime::application_configuration config;
  std::uint64_t clock{}, updates{}, shutdowns{}, closes{};
  bool close{}, allow{};
  config.callbacks.now_ns = [&] { return clock += 10; };
  config.callbacks.poll_events = [&](bool& output) {
    output = close;
    return engine::result::success;
  };
  config.callbacks.close_requested = [&]() {
    ++closes;
    return allow;
  };
  config.callbacks.shutdown = [&] { ++shutdowns; };
  config.callbacks.update = [&](const engine::frame_time& time) {
    check(time.frame_index == updates && (updates == 0 || time.delta_ns == 10));
    engine::ui_draw_view ui;
    ui.display_width = 640;
    ui.display_height = 480;
    check(app->submit_ui_draw_list(ui).ok());
    check(app->submit_debug_draw_list({}).ok());
    if (++updates == 3)
      app->request_exit();
    return engine::result::success;
  };
  check(runtime::application::create(config, app).ok());
  std::uint32_t width{}, height{};
  check(app->get_window_size(width, height).ok() && width == 1280 && height == 720);
  engine::input_event event;
  check(app->poll_input(event) == engine::result::not_ready &&
        event.type == engine::event_type::none);
  check(app->keyboard_state().modifiers == 0 && app->keyboard_state().pressed_keys[0] == 0);
  check(app->pointer_state().buttons == 0 && !app->pointer_state().is_inside);
  check(app->run(8).ok() && updates == 3);
  app.reset();
  check(shutdowns == 1);
  close = allow = true;
  updates = closes = 0;
  check(runtime::application::create(config, app).ok());
  check(app->run(8).ok() && updates == 0 && closes == 1);
  app.reset();
  allow = false;
  closes = 0;
  check(runtime::application::create(config, app).ok());
  check(app->run(8).ok() && updates == 3 && closes == 3);
  app.reset();
  check(shutdowns == 3);
}
void action_maps() {
  runtime::application_configuration config;
  config.asset_root = GNEISS_TEST_ASSET_ROOT;
  std::shared_ptr<runtime::application> app, other;
  check(runtime::application::create(config, app).ok());
  constexpr auto uri = "asset://input/default.input-map.json";
  engine::action_id action;
  engine::action_state state;
  check(app->load_action_map(uri).ok());
  check(app->find_action("move_horizontal", action).ok() && action.value != 0);
  check(app->get_action_state(action, state).ok() && !state.held && state.value == 0);
  check(app->load_action_map("asset://input/missing.input-map.json") == engine::result::not_found);
  check(app->get_action_state(action, state).ok());
  check(app->load_action_map(uri).ok());
  check(app->get_action_state(action, state) == engine::result::invalid_handle);
  // 使用当前仍有效的动作验证跨 Application 拒绝，避免只验证旧代次。
  check(app->find_action("move_horizontal", action).ok());
  check(runtime::application::create(config, other).ok());
  check(other->load_action_map(uri).ok());
  check(other->get_action_state(action, state) == engine::result::invalid_handle);
}
void creation_failures() {
  runtime::application_configuration config;
  std::shared_ptr<runtime::application> app;
  unsigned diagnostics{};
  engine::result reported;
  config.callbacks.diagnostic = [&](const engine::diagnostic_view& event) {
    check(!event.message.empty());
    ++diagnostics;
    reported = event.result;
  };
  config.use_granit_window = true;
  config.window.width = 0;
  check(runtime::application::create(config, app) == engine::result::invalid_argument);
  // 配置校验在创建对象前返回结果，不经过运行期诊断回调。
  check(!app && diagnostics == 0);
  config.use_granit_window = false;
  config.window.width = 1280;
  config.asset_root = "gneiss-test-directory-does-not-exist";
  diagnostics = 0;
  check(runtime::application::create(config, app) == engine::result::not_found);
  check(!app && diagnostics == 1 && reported == engine::result::not_found);
#ifdef GNEISS_TEST_NO_GRANIT_PLATFORM
  config.asset_root = {};
  config.use_granit_window = true;
  diagnostics = 0;
  check(runtime::application::create(config, app) == engine::result::unsupported);
  check(!app && diagnostics == 1 && reported == engine::result::unsupported);
#endif
}
}
int main() try {
  loop();
  action_maps();
  creation_failures();
  return 0;
} catch (...) {
  return 1;
}
