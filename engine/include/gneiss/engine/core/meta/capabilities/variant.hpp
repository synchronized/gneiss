// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/capabilities/container.hpp>

namespace gneiss::meta::cap {
/** 不依赖实例的分支类型与标签查询。 */
class variant : public container<type_kind::variant, basic_variant_view> {
public:
  using container::container;
  [[nodiscard]] GNEISS_META_API std::size_t alternative_count() const noexcept;
  [[nodiscard]] GNEISS_META_API std::span<const std::string_view> names() const noexcept;
  /** 越界返回 invalid_argument；不要求分支已注册。 */
  [[nodiscard]] GNEISS_META_API result<meta::type> alternative(std::size_t) const;
};
}
