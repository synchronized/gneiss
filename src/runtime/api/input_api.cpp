// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/input_conversion.hpp"
#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"
#include "runtime/application_registry.hpp"

#include <gneiss/engine/input.h>

#include <cstddef>
#include <limits>

namespace {
using gneiss::runtime::find_application;
using gneiss::runtime::validate_application;
} // namespace

extern "C" gneiss_result gneiss_application_poll_input(gneiss_application application,
                                                       gneiss_input_event* out_event) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_event == nullptr || out_event->struct_size < GNEISS_INPUT_EVENT_VERSION_1_SIZE) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      gneiss::engine::input_event event;
      const auto operation = state->poll_input(event);
      *out_event = gneiss::engine::detail::input_event_to_abi(event);
      return operation;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_get_keyboard_state(gneiss_application application,
                                                               gneiss_keyboard_state* out_state) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_state == nullptr || out_state->struct_size < GNEISS_KEYBOARD_STATE_VERSION_1_SIZE) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      *out_state = gneiss::engine::detail::input_state_to_abi(state->keyboard_state());
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_get_pointer_state(gneiss_application application,
                                                              gneiss_pointer_state* out_state) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_state == nullptr || out_state->struct_size < GNEISS_POINTER_STATE_VERSION_1_SIZE) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      *out_state = gneiss::engine::detail::input_state_to_abi(state->pointer_state());
      return gneiss::engine::result::success;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_load_action_map(gneiss_application application,
                                                            const char* uri, uint64_t uri_length) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (uri == nullptr || uri_length == 0U ||
        uri_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation = validate_application(state);
      if (validation != gneiss::engine::result::success) {
        return validation;
      }
      const auto result = state->load_action_map({uri, static_cast<std::size_t>(uri_length)});
      if (result != gneiss::engine::result::success) {
        state->report(application, gneiss::engine::severity::error, gneiss::engine::category::input,
                      result, "input", "动作映射加载失败");
      }
      return result;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_find_action(gneiss_application application,
                                                        const char* name, uint64_t name_length,
                                                        gneiss_action* out_action) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_action == nullptr || name == nullptr || name_length == 0U ||
        name_length > std::numeric_limits<std::size_t>::max()) {
      return gneiss::engine::result::invalid_argument;
    }
    *out_action = GNEISS_NULL_ACTION;
    try {
      auto state = find_application(application);
      const auto validation = validate_application(state);
      if (validation != gneiss::engine::result::success) {
        return validation;
      }
      gneiss::engine::action_id action;
      const auto operation =
          state->find_action({name, static_cast<std::size_t>(name_length)}, action);
      *out_action = action.value;
      return operation;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result gneiss_application_get_action_state(gneiss_application application,
                                                             gneiss_action action,
                                                             gneiss_action_state* out_state) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (out_state == nullptr || out_state->struct_size < GNEISS_ACTION_STATE_VERSION_1_SIZE) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation = validate_application(state);
      if (validation != gneiss::engine::result::success) {
        return validation;
      }
      gneiss::engine::action_state native;
      const auto operation = state->get_action_state({action}, native);
      *out_state = GNEISS_ACTION_STATE_INIT;
      out_state->pressed = static_cast<std::uint8_t>(native.pressed);
      out_state->held = static_cast<std::uint8_t>(native.held);
      out_state->released = static_cast<std::uint8_t>(native.released);
      out_state->value = native.value;
      return operation;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}
