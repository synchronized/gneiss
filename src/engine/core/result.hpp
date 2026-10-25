// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include <cstdint>
#include <string_view>

namespace gneiss::engine {
/** 内部操作结果。默认 unknown；非负值成功，负值失败。不拥有动态错误文本。
 * 整数仅在边界显式导入/导出，不隐式转换为整数或公共 C++ 结果类型。 */
class result final {
public:
  constexpr result() noexcept = default;
  explicit constexpr result(std::int32_t code) noexcept : code_(code) {}
  [[nodiscard]] constexpr bool ok() const noexcept { return code_ >= 0; }
  [[nodiscard]] constexpr bool failed() const noexcept { return !ok(); }
  [[nodiscard]] explicit constexpr operator bool() const noexcept { return ok(); }
  [[nodiscard]] constexpr std::int32_t code() const noexcept { return code_; }
  [[nodiscard]] std::string_view message() const noexcept;
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
  static const result cancelled;

private:
  std::int32_t code_{-1};
};
inline constexpr result result::success{0};
inline constexpr result result::unknown{-1};
inline constexpr result result::invalid_argument{-2};
inline constexpr result result::invalid_handle{-3};
inline constexpr result result::out_of_memory{-4};
inline constexpr result result::unsupported{-5};
inline constexpr result result::initialization_failed{-6};
inline constexpr result result::dependency_failed{-7};
inline constexpr result result::invalid_state{-8};
inline constexpr result result::not_ready{-9};
inline constexpr result result::internal{-10};
inline constexpr result result::not_found{-11};
inline constexpr result result::io{-12};
inline constexpr result result::cancelled{-13};

/** 返回静态英文文本；未知码保持原值，文本为 unrecognized result。 */
[[nodiscard]] GNEISS_API const char* result_message(result value) noexcept;
inline std::string_view result::message() const noexcept { return result_message(*this); }
} // namespace gneiss::engine
