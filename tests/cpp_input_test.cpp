// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/entity_id.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "runtime/application.hpp"
#include "support/async_input.hpp"
#include <thread>
#include <type_traits>

int main() {
  using gneiss::engine::error;
  static_assert(!std::is_convertible_v<gneiss::entity_id, gneiss::engine::action_id>);
  static_assert(!std::is_convertible_v<std::uint64_t, gneiss::engine::action_id>);
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view uri = "asset://input/default.input-map.json";
  gneiss::runtime::application_configuration config;
  config.asset_root = asset_root;
  gneiss::engine::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::engine::execution_mode::cooperative});
  std::shared_ptr<gneiss::runtime::application> app, other;
  gneiss::engine::action_id action;
  gneiss::engine::action_state state{};
  gneiss::engine::input_event event{};
  if (gneiss::runtime::application::create(config, app).failed() || action.value != 0 ||
      app->get_action_state(action, state) != error::invalid_handle ||
      app->poll_input(event) != error::not_ready || app->keyboard_state().modifiers != 0 ||
      app->pointer_state().buttons != 0 ||
      gneiss::test_support::load_input_for_test(*app, scheduler, uri).failed() ||
      app->find_action("move_horizontal", action).failed() || action.value == 0 ||
      app->get_action_state(action, state).failed() || state.held || state.value != 0.0F) {
    return 1;
  }
  const auto original = action;
  if (app->find_action("missing", action) != error::not_found || action.value != 0 ||
      gneiss::test_support::load_input_for_test(
          *app, scheduler, "asset://input/missing.input-map.json") != error::not_found ||
      app->get_action_state(original, state).failed() ||
      gneiss::runtime::application::create(config, other).failed() ||
      gneiss::test_support::load_input_for_test(*other, scheduler, uri).failed() ||
      other->get_action_state(original, state) != error::invalid_handle) {
    return 2;
  }
  action = original;
  error thread_result, thread_state_result, thread_event_result;
  state.value = 7.0F;
  event.timestamp_ns = 99;
  std::thread worker([&] {
    thread_result = app->find_action("move_horizontal", action);
    thread_state_result = app->get_action_state(action, state);
    thread_event_result = app->poll_input(event);
  });
  worker.join();
  if (thread_result != error::invalid_state || thread_state_result != error::invalid_state ||
      thread_event_result != error::invalid_state || state.value != 7.0F ||
      event.timestamp_ns != 99 || action != original ||
      gneiss::test_support::load_input_for_test(*app, scheduler, uri).failed() ||
      app->get_action_state(original, state) != error::invalid_handle ||
      app->find_action("move_horizontal", action).failed() || action == original ||
      app->get_action_state(action, state).failed()) {
    return 3;
  }
  // 强类型入口共享同一动作状态；默认构造无需 ABI 结构尺寸。
  if (app->get_action_state(action, state).failed() || app->shutdown().failed()) {
    return 4;
  }
  state.value = 7.0F;
  const auto before_failure = action;
  if (app->find_action("move_horizontal", action) != error::invalid_state ||
      action != before_failure || app->get_action_state(action, state) != error::invalid_state ||
      app->poll_input(event) != error::invalid_state || state.value != 7.0F) {
    return 6;
  }
  return 0;
}
