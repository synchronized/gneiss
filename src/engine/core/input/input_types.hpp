// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <array>
#include <cstdint>

namespace gneiss::engine {

/** 物理键采用 USB HID 编号；修饰键与指针按钮采用位集合。 */
inline constexpr std::uint32_t modifier_left_alt_bit = (1U << 4U);
inline constexpr std::uint32_t modifier_left_control_bit = (1U << 2U);
inline constexpr std::uint32_t modifier_left_shift_bit = (1U << 0U);
inline constexpr std::uint32_t modifier_left_super_bit = (1U << 6U);
inline constexpr std::uint32_t modifier_right_alt_bit = (1U << 5U);
inline constexpr std::uint32_t modifier_right_control_bit = (1U << 3U);
inline constexpr std::uint32_t modifier_right_shift_bit = (1U << 1U);
inline constexpr std::uint32_t modifier_right_super_bit = (1U << 7U);
inline constexpr std::uint32_t physical_key_0 = 39U;
inline constexpr std::uint32_t physical_key_1 = 30U;
inline constexpr std::uint32_t physical_key_9 = 38U;
inline constexpr std::uint32_t physical_key_a = 4U;
inline constexpr std::uint32_t physical_key_backspace = 42U;
inline constexpr std::uint32_t physical_key_delete = 76U;
inline constexpr std::uint32_t physical_key_down = 81U;
inline constexpr std::uint32_t physical_key_end = 77U;
inline constexpr std::uint32_t physical_key_enter = 40U;
inline constexpr std::uint32_t physical_key_escape = 41U;
inline constexpr std::uint32_t physical_key_f1 = 58U;
inline constexpr std::uint32_t physical_key_f12 = 69U;
inline constexpr std::uint32_t physical_key_home = 74U;
inline constexpr std::uint32_t physical_key_insert = 73U;
inline constexpr std::uint32_t physical_key_left = 80U;
inline constexpr std::uint32_t physical_key_left_alt = 226U;
inline constexpr std::uint32_t physical_key_left_control = 224U;
inline constexpr std::uint32_t physical_key_left_shift = 225U;
inline constexpr std::uint32_t physical_key_left_super = 227U;
inline constexpr std::uint32_t physical_key_page_down = 78U;
inline constexpr std::uint32_t physical_key_page_up = 75U;
inline constexpr std::uint32_t physical_key_right = 79U;
inline constexpr std::uint32_t physical_key_right_alt = 230U;
inline constexpr std::uint32_t physical_key_right_control = 228U;
inline constexpr std::uint32_t physical_key_right_shift = 229U;
inline constexpr std::uint32_t physical_key_right_super = 231U;
inline constexpr std::uint32_t physical_key_space = 44U;
inline constexpr std::uint32_t physical_key_tab = 43U;
inline constexpr std::uint32_t physical_key_up = 82U;
inline constexpr std::uint32_t physical_key_z = 29U;
inline constexpr std::uint32_t pointer_middle_bit = (1U << 2U);
inline constexpr std::uint32_t pointer_primary_bit = (1U << 0U);
inline constexpr std::uint32_t pointer_secondary_bit = (1U << 1U);
inline constexpr std::uint32_t pointer_x1_bit = (1U << 3U);
inline constexpr std::uint32_t pointer_x2_bit = (1U << 4U);

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
} // namespace gneiss::engine
