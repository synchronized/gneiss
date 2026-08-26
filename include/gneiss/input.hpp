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

} // namespace gneiss

#endif
