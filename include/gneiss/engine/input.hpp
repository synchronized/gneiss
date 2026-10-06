// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_HPP_
#define GNEISS_INPUT_HPP_

#include <gneiss/engine/core/result.hpp>
#include <gneiss/engine/input.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>

namespace gneiss {

/** 输入协议强类型值；显式底层值保留未命名按键或标志，不隐式转换为整数。 */
// NOLINTNEXTLINE(performance-enum-size): 保留输入协议中未命名扩展值的全部 32 位。
enum class physical_key : std::uint32_t {
  unknown = GNEISS_PHYSICAL_KEY_UNKNOWN,
  a = GNEISS_PHYSICAL_KEY_A,
  b = GNEISS_PHYSICAL_KEY_B,
  c = GNEISS_PHYSICAL_KEY_C,
  d = GNEISS_PHYSICAL_KEY_D,
  e = GNEISS_PHYSICAL_KEY_E,
  f = GNEISS_PHYSICAL_KEY_F,
  g = GNEISS_PHYSICAL_KEY_G,
  h = GNEISS_PHYSICAL_KEY_H,
  i = GNEISS_PHYSICAL_KEY_I,
  j = GNEISS_PHYSICAL_KEY_J,
  k = GNEISS_PHYSICAL_KEY_K,
  l = GNEISS_PHYSICAL_KEY_L,
  m = GNEISS_PHYSICAL_KEY_M,
  n = GNEISS_PHYSICAL_KEY_N,
  o = GNEISS_PHYSICAL_KEY_O,
  p = GNEISS_PHYSICAL_KEY_P,
  q = GNEISS_PHYSICAL_KEY_Q,
  r = GNEISS_PHYSICAL_KEY_R,
  s = GNEISS_PHYSICAL_KEY_S,
  t = GNEISS_PHYSICAL_KEY_T,
  u = GNEISS_PHYSICAL_KEY_U,
  v = GNEISS_PHYSICAL_KEY_V,
  w = GNEISS_PHYSICAL_KEY_W,
  x = GNEISS_PHYSICAL_KEY_X,
  y = GNEISS_PHYSICAL_KEY_Y,
  z = GNEISS_PHYSICAL_KEY_Z,
  digit_1 = GNEISS_PHYSICAL_KEY_1,
  digit_2 = GNEISS_PHYSICAL_KEY_2,
  digit_3 = GNEISS_PHYSICAL_KEY_3,
  digit_4 = GNEISS_PHYSICAL_KEY_4,
  digit_5 = GNEISS_PHYSICAL_KEY_5,
  digit_6 = GNEISS_PHYSICAL_KEY_6,
  digit_7 = GNEISS_PHYSICAL_KEY_7,
  digit_8 = GNEISS_PHYSICAL_KEY_8,
  digit_9 = GNEISS_PHYSICAL_KEY_9,
  digit_0 = GNEISS_PHYSICAL_KEY_0,
  enter = GNEISS_PHYSICAL_KEY_ENTER,
  escape = GNEISS_PHYSICAL_KEY_ESCAPE,
  backspace = GNEISS_PHYSICAL_KEY_BACKSPACE,
  tab = GNEISS_PHYSICAL_KEY_TAB,
  space = GNEISS_PHYSICAL_KEY_SPACE,
  f1 = GNEISS_PHYSICAL_KEY_F1,
  f2 = GNEISS_PHYSICAL_KEY_F2,
  f3 = GNEISS_PHYSICAL_KEY_F3,
  f4 = GNEISS_PHYSICAL_KEY_F4,
  f5 = GNEISS_PHYSICAL_KEY_F5,
  f6 = GNEISS_PHYSICAL_KEY_F6,
  f7 = GNEISS_PHYSICAL_KEY_F7,
  f8 = GNEISS_PHYSICAL_KEY_F8,
  f9 = GNEISS_PHYSICAL_KEY_F9,
  f10 = GNEISS_PHYSICAL_KEY_F10,
  f11 = GNEISS_PHYSICAL_KEY_F11,
  f12 = GNEISS_PHYSICAL_KEY_F12,
  insert = GNEISS_PHYSICAL_KEY_INSERT,
  home = GNEISS_PHYSICAL_KEY_HOME,
  page_up = GNEISS_PHYSICAL_KEY_PAGE_UP,
  delete_key = GNEISS_PHYSICAL_KEY_DELETE,
  end = GNEISS_PHYSICAL_KEY_END,
  page_down = GNEISS_PHYSICAL_KEY_PAGE_DOWN,
  right = GNEISS_PHYSICAL_KEY_RIGHT,
  left = GNEISS_PHYSICAL_KEY_LEFT,
  down = GNEISS_PHYSICAL_KEY_DOWN,
  up = GNEISS_PHYSICAL_KEY_UP,
  left_control = GNEISS_PHYSICAL_KEY_LEFT_CONTROL,
  left_shift = GNEISS_PHYSICAL_KEY_LEFT_SHIFT,
  left_alt = GNEISS_PHYSICAL_KEY_LEFT_ALT,
  left_super = GNEISS_PHYSICAL_KEY_LEFT_SUPER,
  right_control = GNEISS_PHYSICAL_KEY_RIGHT_CONTROL,
  right_shift = GNEISS_PHYSICAL_KEY_RIGHT_SHIFT,
  right_alt = GNEISS_PHYSICAL_KEY_RIGHT_ALT,
  right_super = GNEISS_PHYSICAL_KEY_RIGHT_SUPER,
};

/** 输入协议强类型值；显式底层值保留未命名按键或标志，不隐式转换为整数。 */
// NOLINTNEXTLINE(performance-enum-size): 保留输入协议中未命名扩展值的全部 32 位。
enum class logical_key : std::uint32_t {
  none = GNEISS_LOGICAL_KEY_NONE,
  enter = GNEISS_LOGICAL_KEY_ENTER,
  escape = GNEISS_LOGICAL_KEY_ESCAPE,
  backspace = GNEISS_LOGICAL_KEY_BACKSPACE,
  tab = GNEISS_LOGICAL_KEY_TAB,
  space = GNEISS_LOGICAL_KEY_SPACE,
  left = GNEISS_LOGICAL_KEY_LEFT,
  right = GNEISS_LOGICAL_KEY_RIGHT,
  up = GNEISS_LOGICAL_KEY_UP,
  down = GNEISS_LOGICAL_KEY_DOWN,
  home = GNEISS_LOGICAL_KEY_HOME,
  end = GNEISS_LOGICAL_KEY_END,
  page_up = GNEISS_LOGICAL_KEY_PAGE_UP,
  page_down = GNEISS_LOGICAL_KEY_PAGE_DOWN,
  insert = GNEISS_LOGICAL_KEY_INSERT,
  delete_key = GNEISS_LOGICAL_KEY_DELETE,
  f1 = GNEISS_LOGICAL_KEY_F1,
  f2 = GNEISS_LOGICAL_KEY_F2,
  f3 = GNEISS_LOGICAL_KEY_F3,
  f4 = GNEISS_LOGICAL_KEY_F4,
  f5 = GNEISS_LOGICAL_KEY_F5,
  f6 = GNEISS_LOGICAL_KEY_F6,
  f7 = GNEISS_LOGICAL_KEY_F7,
  f8 = GNEISS_LOGICAL_KEY_F8,
  f9 = GNEISS_LOGICAL_KEY_F9,
  f10 = GNEISS_LOGICAL_KEY_F10,
  f11 = GNEISS_LOGICAL_KEY_F11,
  f12 = GNEISS_LOGICAL_KEY_F12,
};

/** 输入协议强类型值；显式底层值保留未命名按键或标志，不隐式转换为整数。 */
enum class key_action : std::uint8_t {
  released = GNEISS_KEY_RELEASED,
  pressed = GNEISS_KEY_PRESSED,
  repeated = GNEISS_KEY_REPEATED,
};

/** 输入协议强类型值；显式底层值保留未命名按键或标志，不隐式转换为整数。 */
// NOLINTNEXTLINE(performance-enum-size): 保留输入协议中未命名扩展值的全部 32 位。
enum class input_modifier : std::uint32_t {
  none = 0,
  left_shift = GNEISS_MODIFIER_LEFT_SHIFT_BIT,
  right_shift = GNEISS_MODIFIER_RIGHT_SHIFT_BIT,
  left_control = GNEISS_MODIFIER_LEFT_CONTROL_BIT,
  right_control = GNEISS_MODIFIER_RIGHT_CONTROL_BIT,
  left_alt = GNEISS_MODIFIER_LEFT_ALT_BIT,
  right_alt = GNEISS_MODIFIER_RIGHT_ALT_BIT,
  left_super = GNEISS_MODIFIER_LEFT_SUPER_BIT,
  right_super = GNEISS_MODIFIER_RIGHT_SUPER_BIT,
  caps_lock = GNEISS_MODIFIER_CAPS_LOCK_BIT,
  num_lock = GNEISS_MODIFIER_NUM_LOCK_BIT,
};

/** 输入协议强类型值；显式底层值保留未命名按键或标志，不隐式转换为整数。 */
// NOLINTNEXTLINE(performance-enum-size): 保留输入协议中未命名扩展值的全部 32 位。
enum class pointer_button : std::uint32_t {
  none = 0,
  primary = GNEISS_POINTER_PRIMARY_BIT,
  secondary = GNEISS_POINTER_SECONDARY_BIT,
  middle = GNEISS_POINTER_MIDDLE_BIT,
  x1 = GNEISS_POINTER_X1_BIT,
  x2 = GNEISS_POINTER_X2_BIT,
};

/** 只允许同一种输入标志相互组合。 */
template <typename T>
concept input_flags = std::is_same_v<T, input_modifier> || std::is_same_v<T, pointer_button>;
template <input_flags T> [[nodiscard]] constexpr T operator|(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
template <input_flags T> [[nodiscard]] constexpr T operator&(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
template <input_flags T> constexpr T& operator|=(T& left, T right) noexcept {
  return left = left | right;
}
template <input_flags T> constexpr T& operator&=(T& left, T right) noexcept {
  return left = left & right;
}
/** 查询指定标志是否全部存在；空标志总是匹配。 */
template <input_flags T> [[nodiscard]] constexpr bool has_flags(T value, T flags) noexcept {
  return (value & flags) == flags;
}

inline constexpr std::size_t input_text_capacity = GNEISS_INPUT_TEXT_CAPACITY;
/** 拥有事件数据的值快照；复制后不依赖 Application 或下一帧。 */
struct key_event {
  physical_key physical = physical_key::unknown;
  logical_key logical = logical_key::none;
  input_modifier modifiers = input_modifier::none;
  key_action action = key_action::released;
};
/** 拥有固定容量 UTF-8 文本；长度以字节计，不要求零终止。 */
struct text_event {
  std::array<char, input_text_capacity> utf8{};
  std::uint32_t length{};
  [[nodiscard]] std::string_view text() const noexcept {
    return {utf8.data(), std::min<std::size_t>(length, utf8.size())};
  }
};
struct pointer_moved_event {
  float x{}, y{}, delta_x{}, delta_y{};
  pointer_button buttons = pointer_button::none;
};
struct pointer_button_event {
  float x{}, y{};
  pointer_button button = pointer_button::none;
  bool pressed{};
  pointer_button buttons = pointer_button::none;
};
struct pointer_wheel_event {
  float x{}, y{}, delta_x{}, delta_y{};
  pointer_button buttons = pointer_button::none;
};
struct pointer_entered_event {};
struct pointer_left_event {};
/** 使用 get_if/visit 访问有效分支，避免读取 C union 的非活动成员。 */
using input_event_data =
    std::variant<std::monostate, key_event, text_event, pointer_moved_event, pointer_button_event,
                 pointer_wheel_event, pointer_entered_event, pointer_left_event>;
struct input_event {
  std::uint64_t window_id{};
  std::uint64_t timestamp_ns{};
  input_event_data data;
};
/** 当前帧键盘快照；未命名但位于 0～255 的物理键仍可查询。 */
struct keyboard_state {
  input_modifier modifiers = input_modifier::none;
  std::array<std::uint64_t, 4> pressed_keys{};
  [[nodiscard]] bool is_pressed(physical_key key) const noexcept {
    const auto index = static_cast<std::uint32_t>(key);
    return index < 256U && (pressed_keys[index / 64U] & (UINT64_C(1) << (index % 64U))) != 0U;
  }
};
/** 拥有当前帧指针状态；坐标沿用 C API 的窗口坐标约定。 */
struct pointer_state {
  pointer_button buttons = pointer_button::none;
  float x{}, y{};
  bool is_inside{};
};
/** 拥有动作当帧快照；不拥有动作映射或 Application。 */
struct action_state {
  bool pressed{}, held{}, released{};
  float value{};
};

/** 非拥有动作标识；动作映射重载或 Application 销毁后失效，不能跨 Application 使用。 */
class action_id final {
public:
  constexpr action_id() noexcept = default;
  explicit constexpr action_id(gneiss_action value) noexcept : value_(value) {}
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != GNEISS_NULL_ACTION; }
  [[nodiscard]] constexpr gneiss_action get() const noexcept { return value_; }
  friend constexpr bool operator==(action_id, action_id) noexcept = default;

private:
  gneiss_action value_ = GNEISS_NULL_ACTION;
};

inline constexpr action_id null_action_id{};

/** 仅限所属线程。无事件返回 not_ready；未知事件已出队并返回 unsupported，失败保留输出。 */
[[nodiscard]] inline result poll_input(gneiss_application application,
                                       input_event& out_event) noexcept;

[[nodiscard]] inline result get_keyboard_state(gneiss_application application,
                                               keyboard_state& out_state) noexcept;

[[nodiscard]] inline result get_pointer_state(gneiss_application application,
                                              pointer_state& out_state) noexcept;

[[nodiscard]] inline result load_action_map(gneiss_application application,
                                            std::string_view uri) noexcept;

/** 仅限所属线程；成功返回借用标识，失败保留 out_action。 */
[[nodiscard]] inline result find_action(gneiss_application application, std::string_view name,
                                        action_id& out_action) noexcept;

/** 输出当帧值快照，不取得动作所有权；失败保留输出，结构尺寸由包装处理。 */
[[nodiscard]] inline result get_action_state(gneiss_application application, action_id value,
                                             action_state& out_state) noexcept;

} // namespace gneiss

#include <gneiss/engine/detail/input.inl>

#endif
