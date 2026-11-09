// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/detail/type_ops.hpp"
#include "engine/core/meta/registry.hpp"
#include <stdexcept>
#include <type_traits>
namespace gneiss::engine::meta {
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
namespace detail {
struct property_view_access;
}
template <bool Mutable> class basic_variant_view;
using variant_view = basic_variant_view<false>;
using mutable_variant_view = basic_variant_view<true>;
class mutable_instance;
/** 只读借用视图。持有注册表，不持有目标对象；父对象销毁、移动或成员地址失效后不可使用。
 * 基础属性保存访问位置，read_value 每次调用 getter 并返回自有副本，不借用 getter 临时值。
 * 共享视图不代表目标线程安全，调用方负责同步。 */
class instance {
public:
  instance() = default;
  /** 类型擦除入口：调用方保证 object 的实际类型匹配 id，且地址在视图使用期间稳定。 */
  static GNEISS_API result<instance> from_object(registry_ref registry, engine::type_id id,
                                                 const void* object) noexcept;
  static result<instance> from_object(engine::type_id id, const void* object) noexcept {
    auto registry = default_registry();
    if (!registry)
      return registry.error();
    return from_object(*registry, id, object);
  }
  template <class T>
  static result<instance> from(const registry_ref& registry, T& object) noexcept {
    return from_access(registry, detail::ops_of<std::remove_cv_t<T>>(), std::addressof(object));
  }
  template <class T> static result<instance> from(T& object) noexcept {
    auto registry = default_registry();
    if (!registry)
      return registry.error();
    return from(*registry, object);
  }
  template <class T>
    requires(!std::is_lvalue_reference_v<T>)
  static result<instance> from(const registry_ref&, T&&) = delete;
  template <class T>
    requires(!std::is_lvalue_reference_v<T>)
  static result<instance> from(T&&) = delete;
  [[nodiscard]] GNEISS_API meta::type type() const noexcept;
  [[nodiscard]] type_kind kind() const noexcept { return kind_; }
  [[nodiscard]] value_kind scalar_kind() const noexcept { return scalar_kind_; }
  [[nodiscard]] engine::guid guid() const noexcept { return type_; }
  [[nodiscard]] engine::type_id type_id() const noexcept { return cpp_type_; }
  [[nodiscard]] GNEISS_API result<sequence_view> as_sequence() const noexcept;
  [[nodiscard]] GNEISS_API result<associative_view> as_associative() const noexcept;
  [[nodiscard]] GNEISS_API result<optional_view> as_optional() const noexcept;
  [[nodiscard]] GNEISS_API result<variant_view> as_variant() const noexcept;
  [[nodiscard]] GNEISS_API result<reference_view> as_reference() const noexcept;
  [[nodiscard]] GNEISS_API result<property_range> properties() const noexcept;
  [[nodiscard]] GNEISS_API result<instance> property(property_id id) const noexcept;
  [[nodiscard]] GNEISS_API result<instance> property(std::string_view name) const noexcept;
  /** 标量按值复制；可变目标的 ref<meta::object 派生类> 共享强引用，不深拷贝。const
   * 目标不转为可变引用。 */
  [[nodiscard]] GNEISS_API result<value> read_value() const noexcept;
  /** 仅对象视图有稳定对象地址；基础属性（含 getter）返回 unsupported。 */
  [[nodiscard]] GNEISS_API result<const void*> object_data() const noexcept;

private:
  friend class mutable_instance;
  friend class meta::property;
  friend struct detail::property_view_access;
  template <bool> friend class basic_sequence_view;
  template <bool> friend class basic_associative_view;
  template <bool> friend class basic_optional_view;
  template <bool> friend class basic_variant_view;
  template <bool> friend class basic_reference_view;
  static GNEISS_API result<instance> from_access(registry_ref, const detail::type_ops&,
                                                 const void*) noexcept;
  const detail::type_ops* access_{};
  registry_ref registry_;
  engine::guid type_{};
  engine::type_id cpp_type_{};
  bool enumeration_ = false;
  type_kind kind_ = type_kind::invalid;
  value_kind scalar_kind_ = value_kind::invalid;
  const void* data_{};
  const void* parent_{};
  engine::guid parent_type_{};
  property_id property_{};
};
/** 显式可写借用视图。setter 校验规则保持不变，不自动回滚外部副作用。
 * 修改完整对象后，业务方仍需执行类型校验；不能假定单字段修改保持所有跨字段约束。 */
class mutable_instance {
public:
  mutable_instance() = default;
  static GNEISS_API result<mutable_instance> from_object(registry_ref registry, engine::type_id id,
                                                         void* object) noexcept;
  static result<mutable_instance> from_object(engine::type_id id, void* object) noexcept {
    auto registry = default_registry();
    if (!registry)
      return registry.error();
    return from_object(*registry, id, object);
  }
  template <class T>
    requires(!std::is_const_v<T>)
  static result<mutable_instance> from(const registry_ref& registry, T& object) noexcept {
    return from_access(registry, detail::ops_of<T>(), std::addressof(object));
  }
  template <class T>
    requires(!std::is_const_v<T>)
  static result<mutable_instance> from(T& object) noexcept {
    auto registry = default_registry();
    if (!registry)
      return registry.error();
    return from(*registry, object);
  }
  [[nodiscard]] GNEISS_API result<mutable_sequence_view> as_sequence() const noexcept;
  [[nodiscard]] GNEISS_API result<mutable_associative_view> as_associative() const noexcept;
  [[nodiscard]] GNEISS_API result<mutable_optional_view> as_optional() const noexcept;
  [[nodiscard]] GNEISS_API result<mutable_variant_view> as_variant() const noexcept;
  [[nodiscard]] GNEISS_API result<mutable_reference_view> as_reference() const noexcept;
  [[nodiscard]] instance as_const() const noexcept { return view_; }
  [[nodiscard]] type_kind kind() const noexcept { return view_.kind(); }
  [[nodiscard]] GNEISS_API result<mutable_instance> property(property_id id) const noexcept;
  [[nodiscard]] GNEISS_API result<mutable_instance> property(std::string_view name) const noexcept;
  [[nodiscard]] meta::type type() const noexcept { return view_.type(); }
  [[nodiscard]] result<value> read_value() const noexcept { return view_.read_value(); }
  [[nodiscard]] GNEISS_API error write_value(const value& input) const noexcept;
  [[nodiscard]] GNEISS_API result<void*> object_data() const noexcept;
  /** 同类型对象或引用复制赋值；受约束属性经 setter 校验整项替换。
   * 普通成员仍直接赋值，引用共享控制块；调用方负责普通成员的业务校验。 */
  [[nodiscard]] GNEISS_API error assign(const instance& source) const noexcept;
  /** 解码赋值；source 类型须匹配。普通成员移动赋值；受约束属性由 setter 借用候选值。 */
  [[nodiscard]] GNEISS_API error assign_object(engine::type_id id, void* source) const noexcept;

private:
  friend struct detail::property_view_access;
  template <bool> friend class basic_sequence_view;
  template <bool> friend class basic_associative_view;
  template <bool> friend class basic_optional_view;
  template <bool> friend class basic_variant_view;
  template <bool> friend class basic_reference_view;
  static GNEISS_API result<mutable_instance> from_access(registry_ref, const detail::type_ops&,
                                                         void*) noexcept;
  instance view_;
  void* data_{};
  void* parent_{};
};

}
#include "engine/core/meta/associative_view.hpp"
#include "engine/core/meta/optional_view.hpp"
#include "engine/core/meta/reference_view.hpp"
#include "engine/core/meta/sequence_view.hpp"

#include "engine/core/meta/variant_view.hpp"
