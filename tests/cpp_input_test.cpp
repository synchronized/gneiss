// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.hpp>

#include <string_view>
#include <thread>
#include <type_traits>

namespace {
bool event_conversion() {
  using namespace gneiss;
  static_assert(!std::is_same_v<input_event, gneiss_input_event>);
  static_assert(!std::is_same_v<action_state, gneiss_action_state>);
  static_assert(!std::is_convertible_v<physical_key, std::uint32_t>);
  static_assert(has_flags(input_modifier::left_shift | input_modifier::right_control,
                          input_modifier::left_shift));
  keyboard_state keyboard;
  keyboard.pressed_keys[3] = UINT64_C(1) << 63U;
  if (!keyboard.is_pressed(static_cast<physical_key>(255U)) ||
      keyboard.is_pressed(static_cast<physical_key>(256U))) {
    return false;
  }
  gneiss_input_event native = GNEISS_INPUT_EVENT_INIT;
  native.type = GNEISS_INPUT_EVENT_KEY;
  native.window_id = 17;
  native.timestamp_ns = 101;
  native.data.key = {
      .physical_key = GNEISS_PHYSICAL_KEY_A,
      .logical_key = GNEISS_LOGICAL_KEY_NONE,
      .modifiers = GNEISS_MODIFIER_LEFT_SHIFT_BIT,
      .action = GNEISS_KEY_PRESSED,
  };
  input_event event;
  if (detail::from_input_event(native, event).failed()) {
    return false;
  }
  const auto* key = std::get_if<key_event>(&event.data);
  if (key == nullptr || key->physical != physical_key::a || key->action != key_action::pressed ||
      key->modifiers != input_modifier::left_shift || event.window_id != 17 ||
      event.timestamp_ns != 101) {
    return false;
  }
  // 未知事件及非法动作不能覆盖先前成功快照。
  native.data.key.action = 99;
  if (detail::from_input_event(native, event) != result::unsupported ||
      !std::holds_alternative<key_event>(event.data)) {
    return false;
  }
  native.type = 999;
  if (detail::from_input_event(native, event) != result::unsupported) {
    return false;
  }
  native.type = GNEISS_INPUT_EVENT_TEXT;
  native.data.text = {};
  native.data.text.length = GNEISS_INPUT_TEXT_CAPACITY;
  std::fill_n(native.data.text.utf8, GNEISS_INPUT_TEXT_CAPACITY, 'x');
  if (detail::from_input_event(native, event).failed()) {
    return false;
  }
  native.data.text.utf8[0] = 'y';
  const auto copied = event;
  const auto* text = std::get_if<text_event>(&copied.data);
  if (text == nullptr || text->text().size() != input_text_capacity || text->text()[0] != 'x') {
    return false;
  }
  native.data.text.length = GNEISS_INPUT_TEXT_CAPACITY + 1;
  if (detail::from_input_event(native, event) != result::invalid_argument ||
      (std::get_if<text_event>(&event.data) == nullptr ||
       std::get_if<text_event>(&event.data)->text()[0] != 'x')) {
    return false;
  }
  native = GNEISS_INPUT_EVENT_INIT;
  native.type = GNEISS_INPUT_EVENT_POINTER_MOVED;
  native.data.pointer_moved.x = 3.0F;
  native.data.pointer_moved.delta_y = -2.0F;
  native.data.pointer_moved.buttons = GNEISS_POINTER_PRIMARY_BIT;
  if (detail::from_input_event(native, event).failed()) {
    return false;
  }
  const auto* moved = std::get_if<pointer_moved_event>(&event.data);
  if (moved == nullptr || moved->x != 3.0F || moved->delta_y != -2.0F ||
      moved->buttons != pointer_button::primary) {
    return false;
  }
  native.type = GNEISS_INPUT_EVENT_POINTER_WHEEL;
  native.data.pointer_wheel = {};
  native.data.pointer_wheel.delta_x = 1.5F;
  if (detail::from_input_event(native, event).failed() ||
      (std::get_if<pointer_wheel_event>(&event.data) == nullptr ||
       std::get_if<pointer_wheel_event>(&event.data)->delta_x != 1.5F)) {
    return false;
  }
  native.type = GNEISS_INPUT_EVENT_POINTER_BUTTON;
  native.data.pointer_button = {};
  native.data.pointer_button.button = GNEISS_POINTER_SECONDARY_BIT;
  native.data.pointer_button.pressed = 1;
  if (detail::from_input_event(native, event).failed()) {
    return false;
  }
  const auto* button = std::get_if<pointer_button_event>(&event.data);
  if (button == nullptr || !button->pressed || button->button != pointer_button::secondary) {
    return false;
  }
  native.type = GNEISS_INPUT_EVENT_POINTER_ENTERED;
  if (detail::from_input_event(native, event).failed() ||
      !std::holds_alternative<pointer_entered_event>(event.data)) {
    return false;
  }
  native.type = GNEISS_INPUT_EVENT_POINTER_LEFT;
  return detail::from_input_event(native, event).ok() &&
         std::holds_alternative<pointer_left_event>(event.data);
}
} // namespace

int main() {
  using gneiss::result;
  if (!event_conversion()) {
    return 10;
  }
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
  gneiss::action_state state{};
  gneiss::keyboard_state keyboard{};
  gneiss::pointer_state pointer{};
  gneiss::input_event event{};
  if (gneiss::application::create(desc, app).failed() || action.is_valid() ||
      app.get_action_state(action, state) != result::invalid_handle ||
      app.poll_input(event) != result::not_ready || app.get_keyboard_state(keyboard).failed() ||
      app.get_pointer_state(pointer).failed() ||
      keyboard.modifiers != gneiss::input_modifier::none ||
      pointer.buttons != gneiss::pointer_button::none || app.load_action_map(uri).failed() ||
      app.find_action("move_horizontal", action).failed() || !action.is_valid() ||
      app.get_action_state(action, state).failed() || state.held || state.value != 0.0F) {
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
  // 强类型入口共享同一动作状态；默认构造无需 ABI 结构尺寸。
  if (gneiss::get_action_state(app.get(), action, state).failed() || app.reset().failed()) {
    return 4;
  }
  state.value = 7.0F;
  const auto before_failure = action;
  if (app.find_action("move_horizontal", action) != result::invalid_handle ||
      action != before_failure || app.get_action_state(action, state) != result::invalid_handle ||
      app.get_keyboard_state(keyboard) != result::invalid_handle ||
      app.get_pointer_state(pointer) != result::invalid_handle ||
      app.poll_input(event) != result::invalid_handle || state.value != 7.0F) {
    return 6;
  }
  return 0;
}
