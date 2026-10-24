// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/entity_id.hpp"
#include "runtime/application.hpp"
#include <thread>
#include <type_traits>

int main() {
  using gneiss::engine::result;
  static_assert(!std::is_convertible_v<gneiss::entity_id, gneiss::engine::action_id>);
  static_assert(!std::is_convertible_v<std::uint64_t, gneiss::engine::action_id>);
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view uri = "asset://input/default.input-map.json";
  gneiss::runtime::application_configuration config;
  config.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> app, other;
  gneiss::engine::action_id action;
  gneiss::engine::action_state state{};
  gneiss::engine::input_event event{};
  if (gneiss::runtime::application::create(config, app).failed() || action.value != 0 ||
      app->get_action_state(action, state) != result::invalid_handle ||
      app->poll_input(event) != result::not_ready || app->keyboard_state().modifiers != 0 ||
      app->pointer_state().buttons != 0 || app->load_action_map(uri).failed() ||
      app->find_action("move_horizontal", action).failed() || action.value == 0 ||
      app->get_action_state(action, state).failed() || state.held || state.value != 0.0F) {
    return 1;
  }
  const auto original = action;
  if (app->find_action("missing", action) != result::not_found || action != original ||
      app->load_action_map("asset://input/missing.input-map.json") != result::not_found ||
      app->get_action_state(action, state).failed() ||
      gneiss::runtime::application::create(config, other).failed() ||
      other->load_action_map(uri).failed() ||
      other->get_action_state(action, state) != result::invalid_handle) {
    return 2;
  }
  result thread_result, thread_state_result, thread_event_result;
  state.value = 7.0F;
  event.timestamp_ns = 99;
  std::thread worker([&] {
    thread_result = app->find_action("move_horizontal", action);
    thread_state_result = app->get_action_state(action, state);
    thread_event_result = app->poll_input(event);
  });
  worker.join();
  if (thread_result != result::invalid_state || thread_state_result != result::invalid_state ||
      thread_event_result != result::invalid_state || state.value != 7.0F ||
      event.timestamp_ns != 99 || action != original || app->load_action_map(uri).failed() ||
      app->get_action_state(original, state) != result::invalid_handle ||
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
  if (app->find_action("move_horizontal", action) != result::invalid_state ||
      action != before_failure || app->get_action_state(action, state) != result::invalid_state ||
      app->poll_input(event) != result::invalid_state || state.value != 7.0F) {
    return 6;
  }
  return 0;
}
