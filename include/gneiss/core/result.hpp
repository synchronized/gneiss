// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_RESULT_HPP_
#define GNEISS_CORE_RESULT_HPP_

#include <gneiss/core/result.h>

#include <cstdint>
#include <string_view>

namespace gneiss {

/** Gneiss C++ API 使用的强类型结果值。非负值表示成功，负值表示失败。 */
struct result final {
  constexpr result() noexcept = default;
  explicit constexpr result(gneiss_result value) noexcept : value_(value) {}

  /** 返回操作是否成功。 */
  [[nodiscard]] constexpr bool ok() const noexcept { return value_ >= GNEISS_SUCCESS; }

  /** 返回操作是否失败。 */
  [[nodiscard]] constexpr bool failed() const noexcept { return !ok(); }

  /** 成功时为 true，失败时为 false。 */
  [[nodiscard]] explicit constexpr operator bool() const noexcept { return ok(); }

  /** 返回底层 C ABI 结果码。 */
  [[nodiscard]] constexpr gneiss_result native() const noexcept { return value_; }

  /** 返回结果码对应的静态英文文本。 */
  [[nodiscard]] std::string_view message() const noexcept { return gneiss_result_message(value_); }

  [[nodiscard]] explicit constexpr operator gneiss_result() const noexcept { return value_; }

  friend constexpr bool operator==(result, result) noexcept = default;

  static const result success;
  static const result unknown;
  static const result invalid_argument;
  static const result invalid_handle;
  static const result out_of_memory;
  static const result unsupported;
  static const result initialization_failed;
  static const result dependency_failed;
  static const result invalid_state;
  static const result not_ready;
  static const result internal;
  static const result not_found;
  static const result io;

private:
  gneiss_result value_{GNEISS_ERROR_UNKNOWN};
};

inline constexpr result result::success{GNEISS_SUCCESS};
inline constexpr result result::unknown{GNEISS_ERROR_UNKNOWN};
inline constexpr result result::invalid_argument{GNEISS_ERROR_INVALID_ARGUMENT};
inline constexpr result result::invalid_handle{GNEISS_ERROR_INVALID_HANDLE};
inline constexpr result result::out_of_memory{GNEISS_ERROR_OUT_OF_MEMORY};
inline constexpr result result::unsupported{GNEISS_ERROR_UNSUPPORTED};
inline constexpr result result::initialization_failed{GNEISS_ERROR_INITIALIZATION_FAILED};
inline constexpr result result::dependency_failed{GNEISS_ERROR_DEPENDENCY_FAILED};
inline constexpr result result::invalid_state{GNEISS_ERROR_INVALID_STATE};
inline constexpr result result::not_ready{GNEISS_ERROR_NOT_READY};
inline constexpr result result::internal{GNEISS_ERROR_INTERNAL};
inline constexpr result result::not_found{GNEISS_ERROR_NOT_FOUND};
inline constexpr result result::io{GNEISS_ERROR_IO};

[[nodiscard]] constexpr gneiss_result to_native(result value) noexcept { return value.native(); }

[[nodiscard]] constexpr result from_native(gneiss_result value) noexcept { return result{value}; }

} // namespace gneiss

#endif
