// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/meta/metadata.hpp"
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

namespace gneiss::meta {
namespace detail {
struct type_ops;
struct property_record;
}
/** 类型内部的字段身份，零无效。 */
class property_id final {
public:
  constexpr property_id() noexcept = default;
  explicit constexpr property_id(std::uint32_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::uint32_t get() const { return value_; }
  [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != 0; }
  friend constexpr bool operator==(property_id, property_id) noexcept = default;

private:
  std::uint32_t value_ = 0;
};
inline constexpr property_id null_property_id{};
/** 注册时约束内存读写；消费侧查询实际 capabilities。 */
enum class property_access : std::uint8_t { read_write, read_only };
// NOLINTNEXTLINE(performance-enum-size): 保留完整 ABI 标志位。
enum class property_capabilities : std::uint32_t { none = 0, readable = 1, writable = 2 };
template <typename T>
concept property_flag_type = std::same_as<T, property_capabilities>;
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
  gneiss::guid type_guid{};
  property_access access = property_access::read_write;
  std::string_view name{};
  meta::metadata extensions{};
};
/** 回调借用实际类型的对象指针；调用方保证对象存活、类型正确和线程访问合法。
 * setter 必须先校验再修改，失败保持对象；注册表不复制对象或回滚任意回调副作用。
 * 上下文和对象借用；getter 输出拥有数据的 value，setter 只在调用期间借用输入 value。 */
struct property_binding {
  value_kind kind = value_kind::invalid;
  error (*getter)(void*, const void*, value&) = nullptr;
  error (*setter)(void*, void*, const value&) = nullptr;
  void* user_data{};
  gneiss::type_id object_type{};
  /** 借用成员地址，仅在父对象存活且地址稳定时有效；可写访问用于解码临时对象。 */
  const void* (*object_getter)(const void*){};
  void* (*object_mutator)(void*){};
  /** 自定义子类型解码成功后，将独立值移入父对象成员。 */
  void (*object_assign)(void*, void*){};
  /** 受约束属性只允许整项替换；失败须保持原对象，不提供可写成员地址。 */
  error (*object_setter)(void*, const void*){};
  ref<void> (*object_construct)(){};
  gneiss::type_id enum_type{};
  const detail::type_ops* access{}; // 静态只读操作表，不持有对象。
};
} // namespace gneiss::meta

namespace gneiss::meta {
class type;
template <bool Mutable> class basic_instance;
using const_instance = basic_instance<false>;
using instance = basic_instance<true>;
class property_range;
/** 冻结属性句柄；保活注册表，不持有被访问对象。 */
class property final {
public:
  property() = default;
  /** 是否指向已发布的属性记录；不表示目标对象仍然存活。 */
  [[nodiscard]] explicit operator bool() const noexcept { return record_ != nullptr; }
  /** 借用冻结扩展元数据；缺少此类扩展返回 nullptr，寿命受句柄保护。 */
  template <class T> [[nodiscard]] const T* extension() const noexcept {
    return static_cast<const T*>(extension(runtime_type_of<T>()));
  }
  [[nodiscard]] GNEISS_API const void* extension(gneiss::type_id) const noexcept;
  [[nodiscard]] GNEISS_API property_id id() const noexcept;
  [[nodiscard]] GNEISS_API std::string_view name() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::guid declaring_type() const noexcept;
  [[nodiscard]] GNEISS_API gneiss::type_id declaring_type_id() const noexcept;
  [[nodiscard]] GNEISS_API meta::type type() const;
  [[nodiscard]] GNEISS_API property_capabilities capabilities() const noexcept;
  [[nodiscard]] GNEISS_API result<const_instance> get(const const_instance&) const;
  [[nodiscard]] GNEISS_API result<instance> get(const instance&) const;

private:
  friend class property_range;
  friend class type;
  property(registry_ref registry, const detail::property_record* record)
      : registry_(std::move(registry)), record_(record) {}
  registry_ref registry_;
  const detail::property_record* record_{};
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
    iterator(registry_ref registry, const detail::property_record* records, std::size_t index)
        : registry_(std::move(registry)), records_(records), index_(index) {}
    registry_ref registry_;
    const detail::property_record* records_{};
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
  friend class type;
  property_range(registry_ref registry, const detail::property_record* records, std::size_t size)
      : registry_(std::move(registry)), records_(records), size_(size) {}
  registry_ref registry_;
  const detail::property_record* records_{};
  std::size_t size_{};
};
}
