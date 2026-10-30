// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/type_id.hpp"
#include "engine/core/value.hpp"
#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine::meta {
enum class type_kind { invalid, scalar, object, sequence, associative, optional };
namespace detail {
struct type_access;
}
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
enum class property_flags : std::uint32_t { none = 0, read_only = 1, required = 2 };
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
struct property_definition {
  property_id id{};
  engine::guid type_guid{};
  property_flags flags = property_flags::none;
  std::string_view name{};
};
/** 自有枚举显示项，注册时复制；不改变类型的业务校验规则。 */
struct enum_entry {
  std::string name;
  value data;
  bool operator==(const enum_entry&) const = default;
};
/** 名称与显示项由冻结 Registry 借出，有效期不超过 Registry。 */
struct property_info {
  property_id id{};
  engine::guid type_guid{};
  property_flags flags = property_flags::none;
  std::string_view name{};
  value_kind kind = value_kind::invalid;
  property_capabilities capabilities = property_capabilities::none;
  engine::type_id object_type{}; // 非零表示借用成员的运行期类型。
  type_kind category = type_kind::invalid;
  std::span<const enum_entry> choices{};
};
/** 回调借用实际类型的对象指针；调用方保证对象存活、类型正确和线程访问合法。
 * setter 必须先校验再修改，失败保持对象；注册表不复制对象或回滚任意回调副作用。
 * 上下文和对象借用；getter 输出拥有数据的 value，setter 只在调用期间借用输入 value。 */
struct property_binding {
  value_kind kind = value_kind::invalid;
  error (*getter)(void*, const void*, value&) = nullptr;
  error (*setter)(void*, void*, const value&) = nullptr;
  void* user_data{};
  engine::type_id object_type{};
  /** 借用成员地址，仅在父对象存活且地址稳定时有效；可写访问用于解码临时对象。 */
  const void* (*object_getter)(const void*){};
  void* (*object_mutator)(void*){};
  /** 自定义子类型解码成功后，将独立值移入父对象成员。 */
  void (*object_assign)(void*, void*){};
  std::vector<enum_entry> choices{};
  const detail::type_access* access{}; // 静态只读操作表，不持有对象。
};
} // namespace gneiss::engine::meta
