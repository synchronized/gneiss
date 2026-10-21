// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_INPUT_SERVICE_HPP_
#define GNEISS_INPUT_INPUT_SERVICE_HPP_

#include "engine/core/input/input_types.hpp"

#include "engine/resource/types/action_map.hpp"

#include <array>
#include <cstddef>

namespace gneiss::engine::input {

class input_service final {
public:
  static constexpr std::size_t event_capacity = 256;

  void begin_frame() noexcept;
  [[nodiscard]] bool push(const engine::core::input::input_event& event) noexcept;
  [[nodiscard]] result poll(engine::core::input::input_event& out_event) noexcept;
  void set_keyboard(const engine::core::input::keyboard_state& state) noexcept {
    keyboard_ = state;
  }
  void set_pointer(const engine::core::input::pointer_state& state) noexcept { pointer_ = state; }
  void clear_focus() noexcept;
  [[nodiscard]] result replace_action_map(engine::resource::action_map map) noexcept;
  [[nodiscard]] result find_action(std::string_view name,
                                   engine::core::input::action_id& out_action) const noexcept;
  [[nodiscard]] result
  get_action_state(engine::core::input::action_id action,
                   engine::core::input::action_state& out_state) const noexcept;
  [[nodiscard]] const engine::core::input::keyboard_state& keyboard() const noexcept {
    return keyboard_;
  }
  [[nodiscard]] const engine::core::input::pointer_state& pointer() const noexcept {
    return pointer_;
  }

private:
  void apply(const engine::core::input::input_event& event) noexcept;
  void update_actions() noexcept;

  std::array<engine::core::input::input_event, event_capacity> events_{};
  std::size_t read_index_{};
  std::size_t event_count_{};
  engine::core::input::keyboard_state keyboard_ = {};
  engine::core::input::pointer_state pointer_ = {};
  engine::resource::action_map action_map_;
  std::vector<engine::core::input::action_state> action_states_;
  std::uint32_t action_generation_{};
};

} // namespace gneiss::engine::input

#endif
