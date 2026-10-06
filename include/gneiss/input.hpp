// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_HPP_
#define GNEISS_INPUT_HPP_

#include <gneiss/core/result.hpp>
#include <gneiss/input.h>

#include <string_view>

namespace gneiss {

using input_event = gneiss_input_event;
using keyboard_state = gneiss_keyboard_state;
using pointer_state = gneiss_pointer_state;
using action = gneiss_action;
using action_state = gneiss_action_state;

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

[[nodiscard]] inline result poll_input(gneiss_application application,
                                       input_event& out_event) noexcept {
  return from_native(gneiss_application_poll_input(application, &out_event));
}

[[nodiscard]] inline result get_keyboard_state(gneiss_application application,
                                               keyboard_state& out_state) noexcept {
  return from_native(gneiss_application_get_keyboard_state(application, &out_state));
}

[[nodiscard]] inline result get_pointer_state(gneiss_application application,
                                              pointer_state& out_state) noexcept {
  return from_native(gneiss_application_get_pointer_state(application, &out_state));
}

[[nodiscard]] inline result load_action_map(gneiss_application application,
                                            std::string_view uri) noexcept {
  return from_native(gneiss_application_load_action_map(application, uri.data(), uri.size()));
}

[[nodiscard]] inline result find_action(gneiss_application application, std::string_view name,
                                        action& out_action) noexcept {
  return from_native(
      gneiss_application_find_action(application, name.data(), name.size(), &out_action));
}

[[nodiscard]] inline result get_action_state(gneiss_application application, action value,
                                             action_state& out_state) noexcept {
  return from_native(gneiss_application_get_action_state(application, value, &out_state));
}

/** 仅限所属线程；成功返回借用标识，失败保留 out_action。 */
[[nodiscard]] inline result find_action(gneiss_application application, std::string_view name,
                                        action_id& out_action) noexcept {
  gneiss_action value = GNEISS_NULL_ACTION;
  const auto status = find_action(application, name, value);
  if (status.ok()) {
    out_action = action_id{value};
  }
  return status;
}

/** 输出当帧值快照，不取得动作所有权；错误与结构尺寸规则同 C API。 */
[[nodiscard]] inline result get_action_state(gneiss_application application, action_id value,
                                             action_state& out_state) noexcept {
  return get_action_state(application, value.get(), out_state);
}

} // namespace gneiss

#endif
