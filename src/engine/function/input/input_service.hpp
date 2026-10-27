// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_INPUT_SERVICE_HPP_
#define GNEISS_INPUT_INPUT_SERVICE_HPP_

#include "engine/core/input/input_types.hpp"
#include "engine/core/result.hpp"

#include "engine/resource/input/action_map.hpp"

#include <array>
#include <cstddef>

namespace gneiss::engine {

class input_service final {
public:
  static constexpr std::size_t event_capacity = 256;

  void begin_frame() noexcept;
  [[nodiscard]] bool push(const gneiss::engine::input_event& event) noexcept;
  [[nodiscard]] gneiss::engine::result poll(gneiss::engine::input_event& out_event) noexcept;
  void set_keyboard(const gneiss::engine::keyboard_state& state) noexcept { keyboard_ = state; }
  void set_pointer(const gneiss::engine::pointer_state& state) noexcept { pointer_ = state; }
  void clear_focus() noexcept;
  [[nodiscard]] gneiss::engine::result replace_action_map(gneiss::engine::action_map map) noexcept;
  [[nodiscard]] gneiss::engine::result
  find_action(std::string_view name, gneiss::engine::action_id& out_action) const noexcept;
  [[nodiscard]] gneiss::engine::result
  get_action_state(gneiss::engine::action_id action,
                   gneiss::engine::action_state& out_state) const noexcept;
  [[nodiscard]] const gneiss::engine::keyboard_state& keyboard() const noexcept {
    return keyboard_;
  }
  [[nodiscard]] const gneiss::engine::pointer_state& pointer() const noexcept { return pointer_; }

private:
  void apply(const gneiss::engine::input_event& event) noexcept;
  void update_actions() noexcept;

  std::array<gneiss::engine::input_event, event_capacity> events_{};
  std::size_t read_index_{};
  std::size_t event_count_{};
  gneiss::engine::keyboard_state keyboard_ = {};
  gneiss::engine::pointer_state pointer_ = {};
  gneiss::engine::action_map action_map_;
  std::vector<gneiss::engine::action_state> action_states_;
  std::uint32_t action_generation_{};
};

} // namespace gneiss::engine

#endif
