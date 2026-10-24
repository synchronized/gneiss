// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/core/type_guid.hpp"
#include "engine/core/value.hpp"
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace gneiss {
/** 类型内部的字段身份，零无效。 */
class field_id final {
public:
  constexpr field_id() noexcept = default;
  explicit constexpr field_id(std::uint32_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::uint32_t get() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }
  friend constexpr bool operator==(field_id, field_id) noexcept = default;

private:
  std::uint32_t value_ = 0;
};
inline constexpr field_id null_field_id{};
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 标志位。
enum class field_flags : std::uint32_t { none = 0, read_only = 1 };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 标志位。
enum class property_capabilities : std::uint32_t { none = 0, readable = 1, writable = 2 };
template <typename T>
concept reflection_flags = std::same_as<T, field_flags> || std::same_as<T, property_capabilities>;
template <reflection_flags T> [[nodiscard]] constexpr T operator|(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
template <reflection_flags T> [[nodiscard]] constexpr T operator&(T left, T right) noexcept {
  return static_cast<T>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
template <reflection_flags T> constexpr T& operator|=(T& left, T right) noexcept {
  return left = left | right;
}
template <reflection_flags T> constexpr T& operator&=(T& left, T right) noexcept {
  return left = left & right;
}
template <reflection_flags T> [[nodiscard]] constexpr bool has_flags(T value, T flags) noexcept {
  return (value & flags) == flags;
}
using property_vec3 = engine::vector3;
using property_quaternion = engine::quaternion;
// 缺省成员值允许 designated 初始化省略字段，避免消费者缺省字段告警。
// NOLINTBEGIN(readability-redundant-member-init)
/** 名称由冻结 Registry 借出，有效期不超过 Registry。 */
struct field_info {
  field_id id{};
  gneiss::engine::type_guid type_guid{};
  field_flags flags = field_flags::none;
  std::string_view name{};
  gneiss::engine::value_kind kind = gneiss::engine::value_kind::invalid;
  property_capabilities capabilities = property_capabilities::none;
};
/** 自有字段数组；名称借用冻结 Registry 的文本，复制/移动不产生临时数组悬空。 */
struct type_info {
  gneiss::engine::type_guid guid{};
  std::uint32_t schema_version = 0;
  std::string_view name{};
  std::vector<field_info> fields{};
};
// NOLINTEND(readability-redundant-member-init)
} // namespace gneiss
