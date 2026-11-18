// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include <gneiss/engine/core/meta/ops/scalar_ops.hpp>
namespace gneiss::meta {
/** 整体值转换能力；不决定类型的结构类别或持久化格式。 */
struct value_ops {
  value (*read)(const void*){};
  error (*write)(void*, const value&){};
};
/** 可为对象特化；write 应先校验输入，失败时保持目标不变。 */
template <class T> struct value_traits {
  static constexpr bool enabled = detail::reflected_value_kind<T>() != value_kind::invalid;
  static value read(const T& input) { return detail::scalar_value(input); }
  static error write(T& target, const value& input) {
    auto decoded = detail::scalar_cast<T>(input);
    if (!decoded)
      return decoded.error();
    target = std::move(*decoded);
    return error::success;
  }
};
namespace detail {
template <class T> const value_ops& value_ops_of() {
  static const value_ops ops{
      [](const void* p) { return value_traits<T>::read(*static_cast<const T*>(p)); },
      [](void* p, const value& input) {
        return value_traits<T>::write(*static_cast<T*>(p), input);
      }};
  return ops;
}
}
}
