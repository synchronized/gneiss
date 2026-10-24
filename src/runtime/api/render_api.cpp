// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/draw_description.hpp"
#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"
#include "runtime/application_registry.hpp"

#include <gneiss/engine/render.h>

namespace {
using gneiss::runtime::find_application;
using gneiss::runtime::validate_application;
} // namespace

extern "C" gneiss_result
gneiss_application_submit_ui_draw_list(gneiss_application application,
                                       const gneiss_ui_draw_list_desc* desc) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (desc == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      if (!state->can_submit_draw_lists()) {
        return gneiss::engine::result::invalid_state;
      }
      gneiss::engine::detail::converted_ui_draw value;
      const auto converted = gneiss::engine::detail::read_ui_draw_description(*desc, value);
      return converted == gneiss::engine::result::success ? state->submit_ui_draw_list(value.view)
                                                          : converted;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}

extern "C" gneiss_result
gneiss_application_submit_debug_draw_list(gneiss_application application,
                                          const gneiss_debug_draw_list_desc* desc) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    if (desc == nullptr) {
      return gneiss::engine::result::invalid_argument;
    }
    try {
      auto state = find_application(application);
      const auto validation_result = validate_application(state);
      if (validation_result != gneiss::engine::result::success) {
        return validation_result;
      }
      if (!state->can_submit_draw_lists()) {
        return gneiss::engine::result::invalid_state;
      }
      std::vector<gneiss::engine::debug_line> value;
      const auto converted = gneiss::engine::detail::read_debug_draw_description(*desc, value);
      return converted == gneiss::engine::result::success ? state->submit_debug_draw_list(value)
                                                          : converted;
    } catch (const std::bad_alloc&) {
      return gneiss::engine::result::out_of_memory;
    } catch (...) {
      return gneiss::engine::result::internal;
    }
  }());
}
