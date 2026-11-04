// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/ops/forward.hpp"
#include "engine/core/ref.hpp"
#include <cstddef>
#include <type_traits>
#include <utility>
namespace gneiss::engine::meta {
/** 仅适配引擎 ref；get 借用地址，owner 返回保活强引用，不接管裸指针。 */
struct reference_ops {
  const detail::type_ops& (*element)();
  const void* (*get)(const void*);
  void* (*get_mut)(void*){};
  ref<const void> (*owner)(const void*){};
  void (*set)(void*, const ref<void>&){};
  void (*reset)(void*){};
};
namespace detail {
template <class T> struct reference_traits {
  static constexpr bool enabled = false;
};
template <class E> struct reference_traits<ref<E>> {
  static constexpr bool enabled = true;
  using element = E;
};
template <class T> const reference_ops& reference_ops_of() {
  using E = typename reference_traits<T>::element;
  static_assert(!std::is_void_v<E>, "Reflected references require a concrete target type");
  static const reference_ops table = [] {
    reference_ops ops{&ops_of<std::remove_const_t<E>>,
                      [](const void* p) -> const void* { return static_cast<const T*>(p)->get(); }};
    if constexpr (!std::is_const_v<E>)
      ops.get_mut = [](void* p) -> void* { return static_cast<T*>(p)->get(); };
    ops.owner = [](const void* p) -> ref<const void> { return *static_cast<const T*>(p); };
    ops.set = [](void* p, const ref<void>& v) {
      *static_cast<T*>(p) = v.template alias<E>(static_cast<E*>(v.get()));
    };
    ops.reset = [](void* p) { static_cast<T*>(p)->reset(); };
    return ops;
  }();
  return table;
}
}
}
