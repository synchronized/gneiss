// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/meta/ops/ops_fwd.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace gneiss::meta {
struct optional_ops {
  const detail::type_ops& (*element)();
  bool (*has_value)(const void*);
  const void* (*get)(const void*);
  void* (*get_mut)(void*);
  void (*reset)(void*);
  void (*ensure)(void*){};
};
namespace detail {
template <class T> struct optional_traits {
  static constexpr bool enabled = false;
};
template <class E> struct optional_traits<std::optional<E>> {
  static constexpr bool enabled = true;
  using element = E;
};
template <class T> const optional_ops& optional_ops_of() {
  using E = typename optional_traits<T>::element;
  static const optional_ops table = [] {
    optional_ops ops{&ops_of<E>,
                     [](const void* p) { return static_cast<const T*>(p)->has_value(); },
                     [](const void* p) -> const void* {
                       const auto& v = *static_cast<const T*>(p);
                       return v ? std::addressof(*v) : nullptr;
                     },
                     [](void* p) -> void* {
                       auto& v = *static_cast<T*>(p);
                       return v ? std::addressof(*v) : nullptr;
                     },
                     [](void* p) { static_cast<T*>(p)->reset(); }};
    if constexpr (std::is_default_constructible_v<E>)
      ops.ensure = [](void* p) {
        auto& v = *static_cast<T*>(p);
        if (!v)
          v.emplace();
      };
    return ops;
  }();
  return table;
}
}
}
