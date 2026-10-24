// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/result.hpp"
#include "engine/core/type_guid.hpp"
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
// NOLINTNEXTLINE(performance-enum-size): 未知值需保留完整 ABI 数值后交给边界验证。
enum class property_kind : std::uint32_t {
  invalid = 0,
  boolean = 1,
  int64 = 2,
  uint64 = 3,
  float32 = 4,
  float64 = 5,
  string = 6,
  type_id = 7,
  vec3 = 8,
  quaternion = 9,
};
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
struct property_vec3 {
  friend constexpr bool operator==(const property_vec3&, const property_vec3&) noexcept = default;
  float x = 0;
  float y = 0;
  float z = 0;
};
struct property_quaternion {
  friend constexpr bool operator==(const property_quaternion&,
                                   const property_quaternion&) noexcept = default;
  float x = 0;
  float y = 0;
  float z = 0;
  float w = 1;
};
/** 属性文本的寿命由 getter 的适配器约定，包装不延长寿命。 */
using property_string = std::string_view;
using property_payload =
    std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, property_string,
                 engine::type_guid, property_vec3, property_quaternion>;
struct property_value {
  property_payload payload;
  [[nodiscard]] property_kind kind() const noexcept {
    return payload.valueless_by_exception() ? property_kind::invalid
                                            : static_cast<property_kind>(payload.index());
  }
};
/** 不透明目标语义由访问器定义；不拥有 Context 或 Object。 */
struct property_target {
  std::uint64_t context = 0;
  std::uint64_t object = 0;
};
// 缺省成员值允许 designated 初始化省略字段，避免消费者缺省字段告警。
// NOLINTBEGIN(readability-redundant-member-init)
/** 名称由冻结 Registry 借出，有效期不超过 Registry。 */
struct field_info {
  field_id id{};
  gneiss::engine::type_guid value_type_id{};
  field_flags flags = field_flags::none;
  std::string_view name{};
  property_kind kind = property_kind::invalid;
  property_capabilities capabilities = property_capabilities::none;
};
/** 自有字段数组；名称借用冻结 Registry 的文本，复制/移动不产生临时数组悬空。 */
struct type_info {
  gneiss::engine::type_guid id{};
  std::uint32_t schema_version = 0;
  std::string_view name{};
  std::vector<field_info> fields{};
};
// NOLINTEND(readability-redundant-member-init)
} // namespace gneiss
