// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/core/meta/ops/ops_fwd.hpp>
#include <gneiss/engine/core/meta/type.hpp>
#include <gneiss/engine/core/base/ref.hpp>
#include <gneiss/engine/core/base/result.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace gneiss::meta {
/** 仅适配引擎 ref；get 借用地址，owner 返回保活强引用，不接管裸指针。 */
struct reference_ops {
  const type_ops& (*element)();
  const void* (*get)(const void*);
  void* (*get_mut)(void*){};
  ref<const void> (*owner)(const void*){};
  void (*set)(void*, const ref<void>&){};
  void (*reset)(void*){};
  meta::type (*dynamic_type)(const registry_ref&, const void*){};
  /** 仅可变 meta::object 引用支持 value 传递；不移除目标 const。 */
  ref<meta::object> (*read_object)(const void*){};
  error (*write_object)(void*, const ref<meta::object>&){};
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
    ops.owner = [](const void* p) -> ref<const void> {
      const auto& v = *static_cast<const T*>(p);
      if constexpr (std::derived_from<std::remove_const_t<E>, meta::object>)
        return v ? v.template alias<const void>(v->dynamic_address()) : ref<const void>{};
      else
        return v;
    };
    ops.dynamic_type = [](const registry_ref& registry, const void* p) -> meta::type {
      const auto& v = *static_cast<const T*>(p);
      if constexpr (std::derived_from<std::remove_const_t<E>, meta::object>)
        return v ? v->dynamic_type(registry)
                 : resolve_type(registry, ops_of<std::remove_const_t<E>>());
      else
        return resolve_type(registry, ops_of<std::remove_const_t<E>>());
    };
    ops.set = [](void* p, const ref<void>& v) {
      *static_cast<T*>(p) = v.template alias<E>(static_cast<E*>(v.get()));
    };
    ops.reset = [](void* p) { static_cast<T*>(p)->reset(); };
    if constexpr (!std::is_const_v<E> && std::derived_from<E, meta::object>) {
      ops.read_object = [](const void* p) -> ref<meta::object> {
        return *static_cast<const T*>(p);
      };
      ops.write_object = [](void* p, const ref<meta::object>& source) -> error {
        if (!source) {
          static_cast<T*>(p)->reset();
          return error::success;
        }
        auto* target = dynamic_cast<E*>(source.get());
        if (!target)
          return error::invalid_argument;
        *static_cast<T*>(p) = source.alias(target);
        return error::success;
      };
    }
    return ops;
  }();
  return table;
}
}
}
