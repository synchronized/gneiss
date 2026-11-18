// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/core/data/property_id.hpp>
#include <gneiss/engine/core/meta/export.hpp>

#include <gneiss/engine/core/data/value.hpp>
#include <gneiss/engine/core/meta/metadata.hpp>
#include <gneiss/engine/core/meta/registry_fwd.hpp>
#include <gneiss/engine/core/meta/type_types.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/types/ref.hpp>
#include <gneiss/engine/core/types/type_id.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/result.hpp>

#include <concepts>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gneiss::meta {
namespace cap {
class object;
}
struct type_ops;
namespace detail {
struct property_record;
}
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
struct property_binding;
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
  [[nodiscard]] GNEISS_META_API const void* extension(gneiss::type_id) const noexcept;
  [[nodiscard]] GNEISS_META_API property_id id() const noexcept;
  [[nodiscard]] GNEISS_META_API std::string_view name() const noexcept;
  [[nodiscard]] GNEISS_META_API gneiss::guid declaring_type() const noexcept;
  [[nodiscard]] GNEISS_META_API gneiss::type_id declaring_type_id() const noexcept;
  [[nodiscard]] GNEISS_META_API meta::type type() const;
  [[nodiscard]] GNEISS_META_API property_capabilities capabilities() const noexcept;
  [[nodiscard]] GNEISS_META_API result<const_instance> get(const const_instance&) const;
  [[nodiscard]] GNEISS_META_API result<instance> get(const instance&) const;

private:
  friend class property_range;
  friend class cap::object;
  template <bool> friend class basic_object_view;
  [[nodiscard]] GNEISS_META_API const property_binding* binding() const noexcept;
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
    [[nodiscard]] GNEISS_META_API property operator*() const noexcept;
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
  [[nodiscard]] GNEISS_META_API property operator[](std::size_t index) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

private:
  friend class type_registry_state;
  friend class cap::object;
  property_range(registry_ref registry, const detail::property_record* records, std::size_t size)
      : registry_(std::move(registry)), records_(records), size_(size) {}
  registry_ref registry_;
  const detail::property_record* records_{};
  std::size_t size_{};
};
}
