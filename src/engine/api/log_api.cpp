// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/api/log_validation.hpp"
#include "engine/api/result_conversion.hpp"
#include "engine/core/result.hpp"

extern "C" gneiss_result gneiss_log_message_validate(const gneiss_log_message* message) {
  return gneiss::engine::detail::to_c_result([&]() -> gneiss::engine::result {
    return gneiss::engine::detail::validate_log_message(message);
  }());
}
