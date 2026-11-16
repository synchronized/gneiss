// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/detail/type_ops.hpp>
#include <gneiss/engine/core/meta/registry.hpp>

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace gneiss::meta {
template <bool Mutable> class basic_sequence_view;
using sequence_view = basic_sequence_view<false>;
using mutable_sequence_view = basic_sequence_view<true>;
template <bool Mutable> class basic_associative_view;
using associative_view = basic_associative_view<false>;
using mutable_associative_view = basic_associative_view<true>;
template <bool Mutable> class basic_optional_view;
using optional_view = basic_optional_view<false>;
using mutable_optional_view = basic_optional_view<true>;
template <bool Mutable> class basic_reference_view;
using reference_view = basic_reference_view<false>;
using mutable_reference_view = basic_reference_view<true>;
template <bool Mutable> class basic_variant_view;
using variant_view = basic_variant_view<false>;
using mutable_variant_view = basic_variant_view<true>;
/** 借用实例：类型与访问位置共享一套表示；Mutable 在编译期限定写入能力。
 * 不保活目标对象。getter 按调用读取；受约束的复合属性仅允许整体替换。 */
template <bool Mutable> class basic_instance {
  using pointer = std::conditional_t<Mutable, void*, const void*>;

public:
  basic_instance() = default;
  [[nodiscard]] const registry_ref& registry() const noexcept { return type_.registry_; }
  [[nodiscard]] meta::type type() const noexcept { return type_; }
  [[nodiscard]] type_kind kind() const noexcept { return type_.kind(); }
  [[nodiscard]] value_kind scalar_kind() const noexcept { return type_.scalar_kind(); }
  [[nodiscard]] gneiss::guid guid() const noexcept { return type_.guid(); }
  [[nodiscard]] gneiss::type_id type_id() const noexcept { return type_.id(); }
  /** 类型擦除入口，调用方保证类型与地址匹配且目标仍然存活。 */
  static GNEISS_API result<basic_instance> from_object(registry_ref, gneiss::type_id, pointer);
  static result<basic_instance> from_object(gneiss::type_id id, pointer object) {
    auto registry = default_registry();
    return registry ? from_object(*registry, id, object) : result<basic_instance>{registry.error()};
  }
  [[nodiscard]] const_instance as_const() const noexcept;
  [[nodiscard]] property_range properties() const noexcept { return type_.properties(); }
  [[nodiscard]] GNEISS_API result<basic_instance> member(property_id) const;
  [[nodiscard]] GNEISS_API result<basic_instance> member(std::string_view) const;
  [[nodiscard]] GNEISS_API result<value> read_value() const;
  [[nodiscard]] GNEISS_API result<pointer> object_data() const noexcept;
  [[nodiscard]] GNEISS_API result<basic_sequence_view<Mutable>> as_sequence() const noexcept;
  [[nodiscard]] GNEISS_API result<basic_associative_view<Mutable>> as_associative() const noexcept;
  [[nodiscard]] GNEISS_API result<basic_optional_view<Mutable>> as_optional() const noexcept;
  [[nodiscard]] GNEISS_API result<basic_reference_view<Mutable>> as_reference() const noexcept;
  [[nodiscard]] GNEISS_API result<basic_variant_view<Mutable>> as_variant() const noexcept;
  [[nodiscard]] bool requires_replacement() const noexcept
    requires(Mutable)
  {
    return parent_ && !writable_ && kind() != type_kind::scalar;
  }
  [[nodiscard]] GNEISS_API error replace(const std::function<error(const instance&)>&) const
    requires(Mutable);
  [[nodiscard]] GNEISS_API error assign(const value&) const
    requires(Mutable);
  template <class T>
    requires(Mutable && detail::reflected_value_kind<std::remove_cv_t<T>>() != value_kind::invalid)
  [[nodiscard]] error assign(const T& input) const {
    return assign(detail::scalar_value(input));
  }
  /** 普通对象按值复制；引用共享控制块；受约束属性调用 setter。 */
  [[nodiscard]] GNEISS_API error assign(const const_instance&) const
    requires(Mutable);
  [[nodiscard]] GNEISS_API error assign_object(gneiss::type_id, void*) const
    requires(Mutable);

private:
  template <bool> friend class basic_instance;
  template <class T> friend result<const_instance> view(const registry_ref&, const T&);
  template <class T>
    requires(!std::is_const_v<T>)
  friend result<instance> view(const registry_ref&, T&);
  template <bool> friend class basic_sequence_view;
  template <bool> friend class basic_associative_view;
  template <bool> friend class basic_optional_view;
  template <bool> friend class basic_variant_view;
  template <bool> friend class basic_reference_view;
  static GNEISS_API result<basic_instance> from_access(registry_ref, const detail::type_ops&,
                                                       pointer);
  [[nodiscard]] const detail::type_ops* access() const noexcept { return type_.access_; }
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
  gneiss::type_id parent_type_{};
  property_id property_{};
  bool writable_ = Mutable;
};

template <bool Mutable> const_instance basic_instance<Mutable>::as_const() const noexcept {
  const_instance out;
  out.type_ = type_;
  out.data_ = data_;
  out.parent_ = parent_;
  out.parent_type_ = parent_type_;
  out.property_ = property_;
  return out;
}

/** 借用只读左值，视图保活注册表但不保活目标对象。 */
template <class T> result<const_instance> view(const registry_ref& registry, const T& object) {
  return const_instance::from_access(registry, detail::ops_of<std::remove_cv_t<T>>(),
                                     std::addressof(object));
}
/** 借用可写左值，沿用目标 getter/setter 与失效规则。 */
template <class T>
  requires(!std::is_const_v<T>)
result<instance> view(const registry_ref& registry, T& object) {
  return instance::from_access(registry, detail::ops_of<T>(), std::addressof(object));
}
template <class T> result<const_instance> view(const T& object) {
  auto registry = default_registry();
  return registry ? view(*registry, object) : result<const_instance>{registry.error()};
}
template <class T>
  requires(!std::is_const_v<T>)
result<instance> view(T& object) {
  auto registry = default_registry();
  return registry ? view(*registry, object) : result<instance>{registry.error()};
}
template <class T>
  requires(!std::is_lvalue_reference_v<T>)
void view(T&&) = delete;
template <class T>
  requires(!std::is_lvalue_reference_v<T>)
void view(const registry_ref&, T&&) = delete;
}
#include <gneiss/engine/core/meta/views/associative_view.hpp>
#include <gneiss/engine/core/meta/views/optional_view.hpp>
#include <gneiss/engine/core/meta/views/reference_view.hpp>
#include <gneiss/engine/core/meta/views/sequence_view.hpp>

#include <gneiss/engine/core/meta/views/variant_view.hpp>
