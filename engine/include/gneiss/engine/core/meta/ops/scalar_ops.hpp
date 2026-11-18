// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/ops/ops_fwd.hpp>
#include <gneiss/engine/core/meta/property.hpp>
#include <gneiss/engine/result.hpp>

#include <limits>

namespace gneiss::meta {
GNEISS_API bool valid_scalar(const value&) noexcept;
struct scalar_ops {
  value_kind kind = value_kind::invalid;
};
namespace detail {
template <class T, bool = std::is_enum_v<T>> struct integer_base {
  using type = T;
};
template <class T> struct integer_base<T, true> {
  using type = std::underlying_type_t<T>;
};
template <class T> using integer_base_t = typename integer_base<T>::type;
// 整数/枚举借用现有 64 位标量；写入前检查原生底层宽度，枚举定义域由业务校验。
template <class T>
using scalar_storage = std::conditional_t<
    std::is_same_v<T, property_id>, std::uint64_t,
    std::conditional_t<
        std::is_integral_v<integer_base_t<T>> && !std::is_same_v<T, bool>,
        std::conditional_t<std::is_signed_v<integer_base_t<T>>, std::int64_t, std::uint64_t>, T>>;
template <class T> value scalar_value(const T& input) {
  if constexpr (std::is_same_v<T, property_id>)
    return value{std::uint64_t(input.get())};
  else
    return value{static_cast<scalar_storage<T>>(input)};
}
template <class T> result<T> scalar_cast(const value& input) {
  const auto* v = std::get_if<scalar_storage<T>>(&input.payload);
  if (!v)
    return error::invalid_argument;
  if constexpr (std::is_integral_v<integer_base_t<T>> && !std::is_same_v<T, bool>) {
    using Base = integer_base_t<T>;
    if (*v < std::numeric_limits<Base>::lowest() || *v > std::numeric_limits<Base>::max())
      return error::invalid_argument;
  }
  if constexpr (std::is_same_v<T, property_id>) {
    if (*v > UINT32_MAX)
      return error::invalid_argument;
    return property_id{static_cast<std::uint32_t>(*v)};
  } else
    return static_cast<T>(*v);
}
template <class T, std::size_t Index = 1> constexpr value_kind reflected_value_kind() {
  if constexpr (Index == std::variant_size_v<value::storage_type>)
    return value_kind::invalid;
  else if constexpr (std::is_same_v<scalar_storage<T>,
                                    std::variant_alternative_t<Index, value::storage_type>>)
    return is_scalar_value_kind(value::kind_index[Index]) ? value::kind_index[Index]
                                                          : value_kind::invalid;
  else
    return reflected_value_kind<T, Index + 1>();
}

template <class T> const scalar_ops& scalar_ops_of() {
  static const scalar_ops ops{reflected_value_kind<T>()};
  return ops;
}
}
}
