// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/type.hpp>
#include <utility>

namespace gneiss::meta {
template <bool> class basic_sequence_view;
template <bool> class basic_associative_view;
template <bool> class basic_optional_view;
template <bool> class basic_reference_view;
template <bool> class basic_variant_view;
/** 内置容器能力共用绑定协议；仅持有类型上下文，不持有对象。 */
template <type_kind Kind, template <bool> class View> class container_capability {
protected:
  meta::type type_;

public:
  container_capability() = default;
  explicit container_capability(meta::type target)
      : type_(target.kind() == Kind ? std::move(target) : meta::type{}) {}
  [[nodiscard]] explicit operator bool() const noexcept { return bool(type_); }
  /** 保留编译期可写性；类型/注册表不匹配返回 invalid_argument，不支持访问返回 unsupported。 */
  template <bool Mutable>
  [[nodiscard]] GNEISS_API result<View<Mutable>> bind(const basic_instance<Mutable>& source) const;
};
using associative_capability = container_capability<type_kind::associative, basic_associative_view>;
using optional_capability = container_capability<type_kind::optional, basic_optional_view>;
using reference_capability = container_capability<type_kind::reference, basic_reference_view>;
using variant_capability = container_capability<type_kind::variant, basic_variant_view>;
}
