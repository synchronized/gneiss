// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/type.hpp>

namespace gneiss::meta {
template <bool> class basic_object_view;
namespace cap {
/** 结构化对象能力；未注册原生对象仍有效，但没有注册字段。不要求继承 meta::object。 */
class object {
  meta::type type_;

public:
  object() = default;
  explicit object(meta::type target)
      : type_(target.kind() == type_kind::object ? std::move(target) : meta::type{}) {}
  [[nodiscard]] explicit operator bool() const noexcept { return bool(type_); }
  /** 返回保活注册表的字段范围；无能力或未注册时为空。 */
  [[nodiscard]] GNEISS_API property_range properties() const noexcept;
  [[nodiscard]] GNEISS_API property find_property(property_id) const noexcept;
  [[nodiscard]] GNEISS_API property find_property(std::string_view) const noexcept;
  /** 保留实例可写性；不匹配返回 invalid_argument，无法访问返回 unsupported。 */
  template <bool Mutable>
  [[nodiscard]] GNEISS_API result<basic_object_view<Mutable>>
  bind(const basic_instance<Mutable>&) const;
};
}
}
#include <gneiss/engine/core/meta/views/object_view.hpp>
