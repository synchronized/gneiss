// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/instance.hpp>

namespace gneiss::meta {
/** 对象字段借用；不保活宿主，字段借用遵循宿主及容器失效规则。 */
template <bool Mutable> class basic_object_view {
  friend class cap::object;
  friend class property;
  [[nodiscard]] GNEISS_API result<basic_instance<Mutable>> bind_field(const property&) const;
  explicit basic_object_view(basic_instance<Mutable> target) : target_(std::move(target)) {}
  basic_instance<Mutable> target_;

public:
  /** 返回调整基类地址后的借用，保留原实例的只读性；不保活宿主。 */
  [[nodiscard]] GNEISS_API result<basic_instance<Mutable>> cast(meta::type) const;
  [[nodiscard]] GNEISS_API property_range properties() const noexcept;
  [[nodiscard]] GNEISS_API result<basic_instance<Mutable>> member(property_id) const;
  [[nodiscard]] GNEISS_API result<basic_instance<Mutable>> member(std::string_view) const;
};
using object_view = basic_object_view<false>;
using mutable_object_view = basic_object_view<true>;
}
