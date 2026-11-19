// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/export.hpp>

#include <gneiss/engine/core/meta/detail/type_definition.hpp>
#include <utility>

namespace gneiss::meta {
template <bool> class basic_sequence_view;
template <bool> class basic_associative_view;
template <bool> class basic_optional_view;
template <bool> class basic_reference_view;
template <bool> class basic_variant_view;
}
namespace gneiss::meta::cap {
/** 内置容器能力共用绑定协议；仅持有类型上下文，不持有对象。 */
template <type_kind Kind, template <bool> class View> class container {
protected:
  meta::type type_;

private:
  [[nodiscard]] GNEISS_META_API meta::type element_type_impl() const;

public:
  container() = default;
  explicit container(meta::type target)
      : type_(target.kind() == Kind ? std::move(target) : meta::type{}) {}
  [[nodiscard]] explicit operator bool() const noexcept { return bool(type_); }
  /** 要求有效能力；关联容器返回映射值，引用返回声明目标类型。variant 没有单一元素类型。 */
  [[nodiscard]] meta::type element_type() const
    requires(Kind != type_kind::variant)
  {
    return element_type_impl();
  }
  /** 保留编译期可写性；类型/注册表不匹配返回 invalid_argument，不支持访问返回 unsupported。 */
  template <bool Mutable>
  [[nodiscard]] GNEISS_META_API result<View<Mutable>>
  bind(const basic_instance<Mutable>& source) const;
};
using associative = container<type_kind::associative, basic_associative_view>;
using optional = container<type_kind::optional, basic_optional_view>;
using reference = container<type_kind::reference, basic_reference_view>;
using variant = container<type_kind::variant, basic_variant_view>;
}
