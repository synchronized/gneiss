// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <array>
#include <cstdint>

namespace gneiss::engine::core::input {

/** 只在所属输入服务及映射代次内有效；零值无效。 */
struct action_id final {
  std::uint64_t value{};
  friend bool operator==(action_id, action_id) = default;
};

struct action_state final {
  bool pressed{};
  bool held{};
  bool released{};
  float value{};
};

/** 平台与输入服务共享的值类型，不包含 ABI 布局或后端类型。 */
enum class event_type : std::uint32_t {
  none,
  key,
  text,
  pointer_moved,
  pointer_button,
  pointer_wheel,
  pointer_entered,
  pointer_left
};
enum class key_action : std::uint32_t { released, pressed, repeated };

/** 键码沿用物理键编号，修饰键和按钮保留位集合语义。 */
struct key_event {
  std::uint32_t physical_key, logical_key, modifiers;
  key_action action;
};
struct text_event {
  std::uint32_t length;
  char utf8[48];
};
struct pointer_motion {
  float x, y, delta_x, delta_y;
  std::uint32_t buttons;
};
struct pointer_button_event {
  float x, y;
  std::uint32_t button;
  bool pressed;
  std::uint32_t buttons;
};
union event_data {
  key_event key;
  text_event text;
  pointer_motion pointer_moved;
  pointer_button_event pointer_button;
  pointer_motion pointer_wheel;
};
/** type 指定 data 的活动成员；文本长度不得超过 utf8 容量。 */
struct input_event {
  event_type type{};
  std::uint64_t window_id{}, timestamp_ns{};
  event_data data{};
};
struct keyboard_state {
  std::uint32_t modifiers{};
  std::array<std::uint64_t, 4> pressed_keys{};
};
struct pointer_state {
  std::uint32_t buttons{};
  float x{}, y{};
  bool is_inside{};
};
} // namespace gneiss::engine::core::input
