// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <concepts>
#include <cstdint>

namespace gneiss::engine::meta {
/** 类型内部的字段身份，零无效。 */
class property_id final {
public:
  constexpr property_id() noexcept = default;
  explicit constexpr property_id(std::uint32_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::uint32_t get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }
  friend constexpr bool operator==(property_id, property_id) noexcept = default;

private:
  std::uint32_t value_ = 0;
};
inline constexpr property_id null_property_id{};
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 标志位。
enum class property_flags : std::uint32_t { none = 0, read_only = 1 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 标志位。
enum class property_capabilities : std::uint32_t { none = 0, readable = 1, writable = 2 };
template <typename T>
concept property_flag_type =
    std::same_as<T, property_flags> || std::same_as<T, property_capabilities>;
template <property_flag_type T> [[nodiscard]] constexpr T operator|(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
template <property_flag_type T> [[nodiscard]] constexpr T operator&(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
template <property_flag_type T> constexpr T& operator|=(T& left, T right) noexcept {
  return left = left | right;
}
template <property_flag_type T> constexpr T& operator&=(T& left, T right) noexcept {
  return left = left & right;
}
template <property_flag_type T> [[nodiscard]] constexpr bool has_flags(T value, T flags) noexcept {
  return (value & flags) == flags;
}
} // namespace gneiss::engine::meta
