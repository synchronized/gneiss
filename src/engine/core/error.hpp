// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/export.hpp"
#include <cstdint>
#include <string_view>

namespace gneiss::engine {
/** 轻量错误状态，使用 ok()/failed() 显式判断。默认
 * unknown；非负值成功，负值失败。不拥有动态错误文本。
 * 整数仅在边界显式导入/导出，不隐式转换为整数或公共 C++ 结果类型。 */
class error final {
public:
  constexpr error() noexcept = default;
  explicit constexpr error(std::int32_t code) noexcept : code_(code) {}
  [[nodiscard]] constexpr bool ok() const noexcept { return code_ >= 0; }
  [[nodiscard]] constexpr bool failed() const noexcept { return !ok(); }
  [[nodiscard]] constexpr std::int32_t code() const noexcept { return code_; }
  [[nodiscard]] std::string_view message() const noexcept;
  friend constexpr bool operator==(error, error) noexcept = default;
  static const error success;
  static const error unknown;
  static const error invalid_argument;
  static const error invalid_handle;
  static const error out_of_memory;
  static const error unsupported;
  static const error initialization_failed;
  static const error dependency_failed;
  static const error invalid_state;
  static const error not_ready;
  static const error internal;
  static const error not_found;
  static const error io;
  static const error cancelled;

private:
  std::int32_t code_{-1};
};
inline constexpr error error::success{0};
inline constexpr error error::unknown{-1};
inline constexpr error error::invalid_argument{-2};
inline constexpr error error::invalid_handle{-3};
inline constexpr error error::out_of_memory{-4};
inline constexpr error error::unsupported{-5};
inline constexpr error error::initialization_failed{-6};
inline constexpr error error::dependency_failed{-7};
inline constexpr error error::invalid_state{-8};
inline constexpr error error::not_ready{-9};
inline constexpr error error::internal{-10};
inline constexpr error error::not_found{-11};
inline constexpr error error::io{-12};
inline constexpr error error::cancelled{-13};

/** 返回静态英文文本；未知码保持原值，文本为 unrecognized result。 */
[[nodiscard]] GNEISS_API const char* error_message(error value) noexcept;
inline std::string_view error::message() const noexcept { return error_message(*this); }
} // namespace gneiss::engine
