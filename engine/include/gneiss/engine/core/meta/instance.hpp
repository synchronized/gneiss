// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/export.hpp>

#include <gneiss/engine/core/meta/ops/type_ops.hpp>
#include <gneiss/engine/core/meta/registry.hpp>

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace gneiss::meta {
namespace detail {
struct instance_access;
}
template <bool Mutable> class basic_sequence_view;
using const_sequence_view = basic_sequence_view<false>;
using sequence_view = basic_sequence_view<true>;
template <bool Mutable> class basic_associative_view;
using const_associative_view = basic_associative_view<false>;
using associative_view = basic_associative_view<true>;
template <bool Mutable> class basic_optional_view;
using const_optional_view = basic_optional_view<false>;
using optional_view = basic_optional_view<true>;
template <bool Mutable> class basic_reference_view;
using const_reference_view = basic_reference_view<false>;
using reference_view = basic_reference_view<true>;
template <bool Mutable> class basic_variant_view;
using const_variant_view = basic_variant_view<false>;
using variant_view = basic_variant_view<true>;
/** 借用实例：类型与访问位置共享一套表示；Mutable 在编译期限定写入能力。
 * 不保活目标对象。getter 按调用读取；受约束的复合属性仅允许整体替换。 */
template <bool Mutable> class basic_instance {
  using pointer = std::conditional_t<Mutable, void*, const void*>;

public:
  basic_instance() = default;
  [[nodiscard]] const registry_ref& registry() const noexcept { return type_.registry_; }
  [[nodiscard]] meta::type type() const noexcept { return type_; }
  /** 从已解析类型借用对象；支持无 C++ ID 的动态描述，调用方保证地址与描述匹配。
   * 空地址返回 invalid_argument；空类型或非对象类型返回 not_found，不隐式解析类型。 */
  static GNEISS_META_API result<basic_instance> from_object(meta::type, pointer);
  [[nodiscard]] const_instance as_const() const noexcept;
  /** 要求有效实例及存活借用；不支持的类型返回 unsupported，非法标量内容返回
   * invalid_argument；getter 的可恢复失败原样返回。空对象强引用是合法值。 */
  [[nodiscard]] GNEISS_META_API result<value> read_value() const;
  [[nodiscard]] GNEISS_META_API result<pointer> object_data() const noexcept;
  /** 查询能力并绑定本实例；返回类型保留当前只读/可写属性。 */
  template <class Capability> [[nodiscard]] auto capability() const {
    return type_.template capability<Capability>().bind(*this);
  }
  [[nodiscard]] GNEISS_META_API result<basic_sequence_view<Mutable>> as_sequence() const noexcept;
  [[nodiscard]] GNEISS_META_API result<basic_associative_view<Mutable>>
  as_associative() const noexcept;
  [[nodiscard]] GNEISS_META_API result<basic_optional_view<Mutable>> as_optional() const noexcept;
  [[nodiscard]] GNEISS_META_API result<basic_reference_view<Mutable>> as_reference() const noexcept;
  [[nodiscard]] GNEISS_META_API result<basic_variant_view<Mutable>> as_variant() const noexcept;
  /** 要求有效目标实例；类型/内容不匹配、能力缺失及 setter 业务拒绝仍返回错误。 */
  [[nodiscard]] GNEISS_META_API error assign(const value&) const
    requires(Mutable);
  template <class T>
    requires(Mutable && detail::reflected_value_kind<std::remove_cv_t<T>>() != value_kind::invalid)
  [[nodiscard]] error assign(const T& input) const {
    return assign(detail::scalar_value(input));
  }
  /** 要求有效的来源与目标实例。普通对象按值复制；引用共享控制块；受约束属性调用 setter。 */
  [[nodiscard]] GNEISS_META_API error assign(const const_instance&) const
    requires(Mutable);
  /** 允许移动来源对象；来源必须可直接写入。受约束目标经过 setter，来源不保证被消耗。 */
  [[nodiscard]] GNEISS_META_API error move_from(const instance&) const
    requires(Mutable);

private:
  friend struct detail::instance_access;
  template <bool> friend class basic_instance;
  friend class cap::representation;
  friend class cap::object;
  template <bool> friend class basic_object_view;
  template <type_kind, template <bool> class> friend class cap::container;
  template <class T> friend const_instance view(const registry_ref&, const T&);
  template <class T>
    requires(!std::is_const_v<T>)
  friend instance view(const registry_ref&, T&);
  template <bool> friend class basic_sequence_view;
  template <bool> friend class basic_associative_view;
  template <bool> friend class basic_optional_view;
  template <bool> friend class basic_variant_view;
  template <bool> friend class basic_reference_view;
  static GNEISS_META_API basic_instance from_access(registry_ref, const type_ops&, pointer);
  [[nodiscard]] const type_ops* access() const noexcept { return type_.access_; }
  [[nodiscard]] void* mutable_data() const noexcept
    requires(Mutable)
  {
    return writable_ ? const_cast<void*>(data_) : nullptr;
  }
  [[nodiscard]] void* mutable_parent() const noexcept
    requires(Mutable)
  {
    return const_cast<void*>(parent_);
  }
  meta::type type_;
  const void* data_{};
  const void* parent_{};
  const property_ops* binding_{};
  bool writable_ = Mutable;
};

template <bool Mutable> const_instance basic_instance<Mutable>::as_const() const noexcept {
  const_instance out;
  out.type_ = type_;
  out.data_ = data_;
  out.parent_ = parent_;
  out.binding_ = binding_;
  return out;
}

/** 借用只读左值，视图保活注册表但不保活目标对象。 */
template <class T> const_instance view(const registry_ref& registry, const T& object) {
  return const_instance::from_access(registry, detail::ops_of<std::remove_cv_t<T>>(),
                                     std::addressof(object));
}
/** 借用可写左值，沿用目标 getter/setter 与失效规则。 */
template <class T>
  requires(!std::is_const_v<T>)
instance view(const registry_ref& registry, T& object) {
  return instance::from_access(registry, detail::ops_of<T>(), std::addressof(object));
}
template <class T> const_instance view(const T& object) { return view(default_registry(), object); }
template <class T>
  requires(!std::is_const_v<T>)
instance view(T& object) {
  return view(default_registry(), object);
}
template <class T>
  requires(!std::is_lvalue_reference_v<T>)
void view(T&&) = delete;
template <class T>
  requires(!std::is_lvalue_reference_v<T>)
void view(const registry_ref&, T&&) = delete;
}
#include <gneiss/engine/core/meta/views/associative_view.hpp>
#include <gneiss/engine/core/meta/views/object_view.hpp>
#include <gneiss/engine/core/meta/views/optional_view.hpp>
#include <gneiss/engine/core/meta/views/reference_view.hpp>
#include <gneiss/engine/core/meta/views/sequence_view.hpp>

#include <gneiss/engine/core/meta/views/variant_view.hpp>
