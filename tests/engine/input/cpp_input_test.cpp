// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_input.hpp"
#include "../../support/value_result.hpp"
#include <gneiss/engine/core/tasks/scheduler.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/input/service.hpp>
#include <gneiss/engine/function/scene/entity_id.hpp>
#include <type_traits>

int main() {
  using gneiss::error;
  static_assert(!std::is_convertible_v<gneiss::entity_id, gneiss::input::action_id>);
  static_assert(!std::is_convertible_v<std::uint64_t, gneiss::input::action_id>);
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view uri = "asset://input/default.input-map.json";
  gneiss::app::configuration config;
  config.asset_root = asset_root;
  gneiss::tasks::scheduler scheduler(
      {.workers = 0, .mode = gneiss::tasks::execution_mode::cooperative});
  config.executor = &scheduler;
  std::shared_ptr<gneiss::app::application> app, other;
  gneiss::input::action_id action;
  gneiss::input::action_state state{};
  gneiss::input::event event{};
  if (gneiss::test_support::assign_value(gneiss::app::application::create(config), app).failed() ||
      action.value != 0 || app->input().get_action_state(action, state) != error::invalid_handle ||
      app->input().poll(event) != error::not_ready || app->input().keyboard().modifiers != 0 ||
      app->input().pointer().buttons != 0 ||
      gneiss::test_support::load_input_for_test(*app, scheduler, uri).failed() ||
      app->input().find_action("move_horizontal", action).failed() || action.value == 0 ||
      app->input().get_action_state(action, state).failed() || state.held || state.value != 0.0F) {
    return 1;
  }
  const auto original = action;
  if (app->input().find_action("missing", action) != error::not_found || action.value != 0 ||
      gneiss::test_support::load_input_for_test(
          *app, scheduler, "asset://input/missing.input-map.json") != error::not_found ||
      app->input().get_action_state(original, state).failed() ||
      gneiss::test_support::assign_value(gneiss::app::application::create(config), other)
          .failed() ||
      gneiss::test_support::load_input_for_test(*other, scheduler, uri).failed() ||
      other->input().get_action_state(original, state) != error::invalid_handle) {
    return 2;
  }
  action = original;
  if (gneiss::test_support::load_input_for_test(*app, scheduler, uri).failed() ||
      app->input().get_action_state(original, state) != error::invalid_handle ||
      app->input().find_action("move_horizontal", action).failed() || action == original ||
      app->input().get_action_state(action, state).failed()) {
    return 3;
  }
  // 强类型入口共享同一动作状态；默认构造无需 ABI 结构尺寸。
  app->request_exit();
  if (app->input().get_action_state(action, state).failed() || app->run().failed()) {
    return 4;
  }
  return 0;
}
