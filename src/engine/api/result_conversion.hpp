// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/result.hpp"
#include <gneiss/engine/core/result.h>

namespace gneiss::engine::api {
/** C ABI 边界无损转换，包括未知码；不隐式转换或重新解释对象布局。 */
[[nodiscard]] constexpr gneiss_result to_c_result(core::result value) noexcept {
  return value.code();
}
[[nodiscard]] constexpr core::result from_c_result(gneiss_result value) noexcept {
  return core::result{value};
}
static_assert(to_c_result(core::result::success) == GNEISS_SUCCESS);
static_assert(to_c_result(core::result::unknown) == GNEISS_ERROR_UNKNOWN);
static_assert(to_c_result(core::result::invalid_argument) == GNEISS_ERROR_INVALID_ARGUMENT);
static_assert(to_c_result(core::result::invalid_handle) == GNEISS_ERROR_INVALID_HANDLE);
static_assert(to_c_result(core::result::out_of_memory) == GNEISS_ERROR_OUT_OF_MEMORY);
static_assert(to_c_result(core::result::unsupported) == GNEISS_ERROR_UNSUPPORTED);
static_assert(to_c_result(core::result::initialization_failed) ==
              GNEISS_ERROR_INITIALIZATION_FAILED);
static_assert(to_c_result(core::result::dependency_failed) == GNEISS_ERROR_DEPENDENCY_FAILED);
static_assert(to_c_result(core::result::invalid_state) == GNEISS_ERROR_INVALID_STATE);
static_assert(to_c_result(core::result::not_ready) == GNEISS_ERROR_NOT_READY);
static_assert(to_c_result(core::result::internal) == GNEISS_ERROR_INTERNAL);
static_assert(to_c_result(core::result::not_found) == GNEISS_ERROR_NOT_FOUND);
static_assert(to_c_result(core::result::io) == GNEISS_ERROR_IO);
} // namespace gneiss::engine::api
