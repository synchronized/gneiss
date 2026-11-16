// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/meta/ops/ops_fwd.hpp>
#include <gneiss/engine/core/types/value.hpp>

#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <variant>

namespace gneiss::meta {
/** 稳定分支名显式配置；供视图查询和持久化使用，未配置时仍支持按索引访问。 */
template <class T> struct variant_case_traits {};
struct variant_ops {
  std::span<const detail::type_ops* const> alternatives;
  std::span<const std::string_view> names;
  bool (*valueless)(const void*);
  std::size_t (*index)(const void*);
  const void* (*get)(const void*);
  void* (*get_mut)(void*);
  error (*select)(void*, std::size_t);
};
template <class T> struct variant_traits {
  static constexpr bool enabled = false;
};
template <class... T> struct variant_traits<std::variant<T...>> {
  static constexpr bool enabled = true;
  using storage_type = std::variant<T...>;
  static auto& storage(storage_type& v) noexcept { return v; }
  static const auto& storage(const storage_type& v) noexcept { return v; }
};
template <> struct variant_traits<gneiss::value> {
  static constexpr bool enabled = true;
  using storage_type = gneiss::value::storage_type;
  static auto& storage(gneiss::value& v) noexcept { return v.payload; }
  static const auto& storage(const gneiss::value& v) noexcept { return v.payload; }
};
template <> struct variant_case_traits<gneiss::value> {
  static constexpr std::array<std::string_view, 22> names{"invalid",
                                                          "bool",
                                                          "int64",
                                                          "uint64",
                                                          "float32",
                                                          "float64",
                                                          "string",
                                                          "guid",
                                                          "vec2",
                                                          "vec3",
                                                          "vec4",
                                                          "quaternion",
                                                          "matrix3",
                                                          "matrix4",
                                                          "object",
                                                          "record",
                                                          "sequence",
                                                          "map",
                                                          "optional",
                                                          "variant",
                                                          "asset_reference",
                                                          "object_reference"};
};
namespace detail {
template <class T, std::size_t I = 0> error select_variant(T& value, std::size_t index) {
  if constexpr (I == std::variant_size_v<T>)
    return error::invalid_argument;
  else {
    if (index == I) {
      if constexpr (std::is_default_constructible_v<std::variant_alternative_t<I, T>>) {
        value.template emplace<I>();
        return error::success;
      } else
        return error::unsupported;
    }
    return select_variant<T, I + 1>(value, index);
  }
}
template <class T> const variant_ops& variant_ops_of() {
  using S = typename variant_traits<T>::storage_type;
  static const auto alternatives = []<std::size_t... I>(std::index_sequence<I...>) {
    return std::array<const type_ops*, sizeof...(I)>{
        &ops_of<std::variant_alternative_t<I, S>>()...};
  }(std::make_index_sequence<std::variant_size_v<S>>{});
  static const variant_ops table = [] {
    variant_ops ops{
        alternatives,
        {},
        [](const void* p) {
          return variant_traits<T>::storage(*static_cast<const T*>(p)).valueless_by_exception();
        },
        [](const void* p) { return variant_traits<T>::storage(*static_cast<const T*>(p)).index(); },
        [](const void* p) -> const void* {
          return std::visit([](const auto& v) -> const void* { return std::addressof(v); },
                            variant_traits<T>::storage(*static_cast<const T*>(p)));
        },
        [](void* p) -> void* {
          return std::visit([](auto& v) -> void* { return std::addressof(v); },
                            variant_traits<T>::storage(*static_cast<T*>(p)));
        },
        [](void* p, std::size_t index) {
          return select_variant(variant_traits<T>::storage(*static_cast<T*>(p)), index);
        }};
    if constexpr (requires { variant_case_traits<T>::names; }) {
      constexpr auto& names = variant_case_traits<T>::names;
      static_assert(names.size() == std::variant_size_v<S>);
      static_assert(
          [] {
            constexpr auto& labels = variant_case_traits<T>::names;
            for (std::size_t i = 0; i < labels.size(); ++i) {
              if (labels[i].empty())
                return false;
              for (std::size_t j = 0; j < i; ++j)
                if (labels[i] == labels[j])
                  return false;
            }
            return true;
          }(),
          "Variant labels must be nonempty and unique");
      ops.names = names;
    }
    return ops;
  }();
  return table;
}
}
}
