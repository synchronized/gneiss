// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/registry_fwd.hpp"
#include "engine/core/meta/type_types.hpp"
#include "engine/core/ref.hpp"
#include "engine/core/result.hpp"
#include "engine/core/type_id.hpp"
#include "engine/core/value.hpp"
#include <concepts>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::engine::meta {
namespace detail {
struct type_ops;
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
enum class property_flags : std::uint32_t {
  none = 0,
  read_only = 1,
  required = 2,
  editor_read_only = 4,
  /** JSON 使用已注册枚举项名称；不改变 C++ 属性类型和 value 的整数表示。 */
  enum_name = 8
};
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
  /** 受约束属性只允许整项替换；失败须保持原对象，不提供可写成员地址。 */
  error (*object_setter)(void*, const void*){};
  ref<void> (*object_construct)(){};
  engine::type_id enum_type{};
  const detail::type_ops* access{}; // 静态只读操作表，不持有对象。
};
} // namespace gneiss::engine::meta

namespace gneiss::engine::meta {
class type;
class const_instance;
class instance;
class property_range;
/** 冻结属性句柄；保活注册表，不持有被访问对象。 */
class property final {
public:
  property() = default;
  [[nodiscard]] GNEISS_API property_id id() const noexcept;
  [[nodiscard]] GNEISS_API std::string_view name() const noexcept;
  [[nodiscard]] GNEISS_API engine::guid declaring_type() const noexcept;
  [[nodiscard]] GNEISS_API engine::type_id declaring_type_id() const noexcept;
  [[nodiscard]] GNEISS_API meta::type type() const noexcept;
  [[nodiscard]] GNEISS_API property_flags flags() const noexcept;
  [[nodiscard]] GNEISS_API property_capabilities capabilities() const noexcept;
  [[nodiscard]] GNEISS_API result<const_instance> get(const const_instance&) const noexcept;
  [[nodiscard]] GNEISS_API result<instance> get(const instance&) const noexcept;

private:
  friend class property_range;
  property(registry_ref registry, const void* record)
      : registry_(std::move(registry)), record_(record) {}
  registry_ref registry_;
  const void* record_{};
};
/** 借用冻结属性数组并保活注册表；遍历不分配、不复制属性列表。 */
class property_range final {
public:
  class iterator {
  public:
    using value_type = property;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    iterator() = default;
    [[nodiscard]] GNEISS_API property operator*() const noexcept;
    struct arrow_proxy {
      property value;
      const property* operator->() const noexcept { return &value; }
    };
    [[nodiscard]] arrow_proxy operator->() const noexcept { return {**this}; }
    iterator& operator++() noexcept {
      ++index_;
      return *this;
    }
    iterator operator++(int) noexcept {
      auto previous = *this;
      ++*this;
      return previous;
    }
    friend bool operator==(const iterator&, const iterator&) = default;

  private:
    friend class property_range;
    iterator(registry_ref registry, const void* records, std::size_t index)
        : registry_(std::move(registry)), records_(records), index_(index) {}
    registry_ref registry_;
    const void* records_{};
    std::size_t index_{};
  };
  property_range() = default;
  [[nodiscard]] iterator begin() const noexcept { return {registry_, records_, 0}; }
  [[nodiscard]] iterator end() const noexcept { return {registry_, records_, size_}; }
  [[nodiscard]] GNEISS_API property operator[](std::size_t index) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

private:
  friend class type_registry_state;
  property_range(registry_ref registry, const void* records, std::size_t size)
      : registry_(std::move(registry)), records_(records), size_(size) {}
  registry_ref registry_;
  const void* records_{};
  std::size_t size_{};
};
}
