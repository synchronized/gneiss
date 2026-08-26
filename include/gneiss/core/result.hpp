// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_RESULT_HPP_
#define GNEISS_CORE_RESULT_HPP_

#include <gneiss/core/result.h>

#include <cstdint>
#include <string_view>

namespace gneiss {

/** Gneiss C++ API 使用的强类型结果码。 */
enum class result : std::int32_t {
  success = GNEISS_SUCCESS,
  unknown = GNEISS_ERROR_UNKNOWN,
  invalid_argument = GNEISS_ERROR_INVALID_ARGUMENT,
  invalid_handle = GNEISS_ERROR_INVALID_HANDLE,
  out_of_memory = GNEISS_ERROR_OUT_OF_MEMORY,
  unsupported = GNEISS_ERROR_UNSUPPORTED,
  initialization_failed = GNEISS_ERROR_INITIALIZATION_FAILED,
  dependency_failed = GNEISS_ERROR_DEPENDENCY_FAILED,
  invalid_state = GNEISS_ERROR_INVALID_STATE,
  not_ready = GNEISS_ERROR_NOT_READY,
  internal = GNEISS_ERROR_INTERNAL,
};

[[nodiscard]] constexpr gneiss_result to_native(result value) noexcept {
  return static_cast<gneiss_result>(value);
}

[[nodiscard]] constexpr result from_native(gneiss_result value) noexcept {
  return static_cast<result>(value);
}

[[nodiscard]] constexpr bool succeeded(result value) noexcept {
  return to_native(value) >= GNEISS_SUCCESS;
}

[[nodiscard]] constexpr bool failed(result value) noexcept { return !succeeded(value); }

[[nodiscard]] inline std::string_view result_message(result value) noexcept {
  return gneiss_result_message(to_native(value));
}

} // namespace gneiss

#endif
