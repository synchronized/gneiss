// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include "engine/core/meta/ops/forward.hpp"
#include <array>
#include <memory>
#include <span>
#include <string_view>
#include <variant>
namespace gneiss::engine::meta {
/** 文件标签显式配置；不配置时仍支持视图访问，JSON 编解码返回 unsupported。 */
template <class T> struct variant_serialization_traits {};
struct variant_ops {
  std::span<const detail::type_ops* const> alternatives;
  std::span<const std::string_view> names;
  bool (*valueless)(const void*);
  std::size_t (*index)(const void*);
  const void* (*get)(const void*);
  void* (*get_mut)(void*);
  error (*select)(void*, std::size_t);
};
namespace detail {
template <class T> struct variant_traits {
  static constexpr bool enabled = false;
};
template <class... T> struct variant_traits<std::variant<T...>> {
  static constexpr bool enabled = true;
};
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
  static const auto alternatives = []<std::size_t... I>(std::index_sequence<I...>) {
    return std::array<const type_ops*, sizeof...(I)>{
        &ops_of<std::variant_alternative_t<I, T>>()...};
  }(std::make_index_sequence<std::variant_size_v<T>>{});
  static const variant_ops table = [] {
    variant_ops ops{
        alternatives,
        {},
        [](const void* p) { return static_cast<const T*>(p)->valueless_by_exception(); },
        [](const void* p) { return static_cast<const T*>(p)->index(); },
        [](const void* p) -> const void* {
          return std::visit([](const auto& v) -> const void* { return std::addressof(v); },
                            *static_cast<const T*>(p));
        },
        [](void* p) -> void* {
          return std::visit([](auto& v) -> void* { return std::addressof(v); },
                            *static_cast<T*>(p));
        },
        [](void* p, std::size_t index) { return select_variant(*static_cast<T*>(p), index); }};
    if constexpr (requires { variant_serialization_traits<T>::names; }) {
      constexpr auto& names = variant_serialization_traits<T>::names;
      static_assert(names.size() == std::variant_size_v<T>);
      static_assert(
          [] {
            constexpr auto& labels = variant_serialization_traits<T>::names;
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
