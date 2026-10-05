// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>

#include <string_view>
#include <thread>
#include <type_traits>

int main() {
  using gneiss::result;
  static_assert(!std::is_convertible_v<gneiss::entity_id, gneiss::action_id>);
  static_assert(!std::is_convertible_v<gneiss_action, gneiss::action_id>);
  constexpr std::string_view asset_root = GNEISS_TEST_ASSET_ROOT;
  constexpr std::string_view uri = "asset://input/default.input-map.json";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss::application app;
  gneiss::application other;
  gneiss::action_id action;
  gneiss::action_state state = GNEISS_ACTION_STATE_INIT;
  gneiss::keyboard_state keyboard = GNEISS_KEYBOARD_STATE_INIT;
  gneiss::pointer_state pointer = GNEISS_POINTER_STATE_INIT;
  gneiss::input_event event = GNEISS_INPUT_EVENT_INIT;
  if (gneiss::application::create(desc, app).failed() || action.is_valid() ||
      app.get_action_state(action, state) != result::invalid_handle ||
      app.poll_input(event) != result::not_ready || app.get_keyboard_state(keyboard).failed() ||
      app.get_pointer_state(pointer).failed() || keyboard.modifiers != 0U ||
      pointer.buttons != 0U || app.load_action_map(uri).failed() ||
      app.find_action("move_horizontal", action).failed() || !action.is_valid() ||
      app.get_action_state(action, state).failed() || state.held != 0U || state.value != 0.0F) {
    return 1;
  }
  const auto original = action;
  if (app.find_action("missing", action) != result::not_found || action != original ||
      app.load_action_map("asset://input/missing.input-map.json") != result::not_found ||
      app.get_action_state(action, state).failed() ||
      gneiss::application::create(desc, other).failed() || other.load_action_map(uri).failed() ||
      other.get_action_state(action, state) != result::invalid_handle) {
    return 2;
  }
  result thread_result;
  std::thread worker([&] { thread_result = app.find_action("move_horizontal", action); });
  worker.join();
  if (thread_result != result::invalid_state || action != original ||
      app.load_action_map(uri).failed() ||
      app.get_action_state(original, state) != result::invalid_handle ||
      app.find_action("move_horizontal", action).failed() || action == original ||
      app.get_action_state(action, state).failed()) {
    return 3;
  }
  // 旧裸句柄入口保留；两种包装查询的是同一动作状态。
  gneiss::action raw{};
  if (gneiss::find_action(app.get(), "move_horizontal", raw).failed() || raw != action.get() ||
      gneiss::get_action_state(app.get(), action, state).failed()) {
    return 4;
  }
  state.struct_size = 0U;
  if (app.get_action_state(action, state) != result::invalid_argument || app.reset().failed()) {
    return 5;
  }
  state = GNEISS_ACTION_STATE_INIT;
  const auto before_failure = action;
  if (app.find_action("move_horizontal", action) != result::invalid_handle ||
      action != before_failure || app.get_action_state(action, state) != result::invalid_handle ||
      app.get_keyboard_state(keyboard) != result::invalid_handle ||
      app.get_pointer_state(pointer) != result::invalid_handle ||
      app.poll_input(event) != result::invalid_handle) {
    return 6;
  }
  return 0;
}
