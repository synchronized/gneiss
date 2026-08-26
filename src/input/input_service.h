// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_INPUT_INPUT_SERVICE_H_
#define GNEISS_INPUT_INPUT_SERVICE_H_

#include <gneiss/input.h>

#include <array>
#include <cstddef>

namespace gneiss::input_internal {

class input_service final {
public:
  static constexpr std::size_t event_capacity = 256;

  void begin_frame() noexcept;
  [[nodiscard]] bool push(const gneiss_input_event& event) noexcept;
  [[nodiscard]] gneiss_result poll(gneiss_input_event& out_event) noexcept;
  void set_keyboard(const gneiss_keyboard_state& state) noexcept { keyboard_ = state; }
  void set_pointer(const gneiss_pointer_state& state) noexcept { pointer_ = state; }
  void clear_focus() noexcept;
  [[nodiscard]] const gneiss_keyboard_state& keyboard() const noexcept { return keyboard_; }
  [[nodiscard]] const gneiss_pointer_state& pointer() const noexcept { return pointer_; }

private:
  void apply(const gneiss_input_event& event) noexcept;

  std::array<gneiss_input_event, event_capacity> events_{};
  std::size_t read_index_{};
  std::size_t event_count_{};
  gneiss_keyboard_state keyboard_ = GNEISS_KEYBOARD_STATE_INIT;
  gneiss_pointer_state pointer_ = GNEISS_POINTER_STATE_INIT;
};

} // namespace gneiss::input_internal

#endif
