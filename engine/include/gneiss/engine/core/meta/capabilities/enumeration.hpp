// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/export.hpp>

#include <gneiss/engine/core/meta/detail/type_definition.hpp>
#include <utility>

namespace gneiss::meta::cap {
/** 枚举类型能力；未注册原生枚举仍有效，但没有枚举项。 */
class enumeration {
  meta::type type_;

public:
  enumeration() = default;
  explicit enumeration(meta::type target)
      : type_(target.is_enum() ? std::move(target) : meta::type{}) {}
  [[nodiscard]] explicit operator bool() const noexcept { return bool(type_); }
  /** 借用注册枚举项；能力缺失或未注册返回空范围，借用期间须保活能力。 */
  [[nodiscard]] GNEISS_META_API std::span<const enum_entry> entries() const noexcept;
};
}
