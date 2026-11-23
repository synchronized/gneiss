// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_INPUT_SERVICE_HPP_
#define GNEISS_INPUT_INPUT_SERVICE_HPP_

#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/input/action.hpp>
#include <gneiss/engine/platform/window/input.hpp>

#include <gneiss/engine/resource/input/action_map.hpp>

#include <cstddef>
#include <gneiss/engine/export.hpp>
#include <vector>

namespace gneiss::input {

/** 单线程输入上下文；由所有者推进帧和注入事件，借用引用不保活所有者。 */
class service final {
public:
  /** 清空上帧事件及动作边沿，复用缓冲容量；按键保持状态不变。 */
  GNEISS_ENGINE_API void begin_frame() noexcept;
  /** 接收单个输入上下文的有序事件；非法事件返回 false，分配失败正常传播。 */
  [[nodiscard]] GNEISS_ENGINE_API bool push(const event& event);
  [[nodiscard]] GNEISS_ENGINE_API error poll(event& out_event) noexcept;
  GNEISS_ENGINE_API void clear_focus() noexcept;
  [[nodiscard]] GNEISS_ENGINE_API error replace_action_map(action_map_description map);
  [[nodiscard]] GNEISS_ENGINE_API error find_action(std::string_view name,
                                                    action_id& out_action) const noexcept;
  [[nodiscard]] GNEISS_ENGINE_API error get_action_state(action_id action,
                                                         action_state& out_state) const noexcept;
  [[nodiscard]] const keyboard_state& keyboard() const noexcept { return keyboard_; }
  [[nodiscard]] const pointer_state& pointer() const noexcept { return pointer_; }

private:
  void apply(const event& event);
  void update_actions() noexcept;

  std::vector<event> events_;
  std::size_t read_index_{};
  keyboard_state keyboard_ = {};
  pointer_state pointer_ = {};
  action_map_description action_map_;
  std::vector<action_state> action_states_;
  std::uint32_t action_generation_{};
};

} // namespace gneiss::input

#endif
