// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once

#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/meta/ops/ops_fwd.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace gneiss::meta {
/** 专门化以适配自定义序列；size/at 必需，at_mut/resize/max_size/replace 可选。元素地址仅借用。
 * replace(T&, const vector<element>&) 返回 error，负责整组校验，失败保持原集合。 */
template <class T> struct sequence_traits {
  static constexpr bool enabled = false;
};
/** 显式声明序列元素身份；未声明的序列不推测成员名。 */
template <class T> struct sequence_identity_traits {
  static constexpr bool enabled = false;
};
template <class T> struct indexed_sequence_traits {
  static constexpr bool enabled = true;
  using element = typename T::value_type;
  static std::size_t size(const T& v) { return v.size(); }
  static std::size_t max_size(const T& v)
    requires requires { v.max_size(); }
  {
    return v.max_size();
  }
  static const element* at(const T& v, std::size_t i) { return std::addressof(v[i]); }
  static element* at_mut(T& v, std::size_t i) { return std::addressof(v[i]); }
};
template <class E, class A>
struct sequence_traits<std::vector<E, A>> : indexed_sequence_traits<std::vector<E, A>> {
  static_assert(!std::is_same_v<E, bool>, "Proxy references are not supported");
  static void resize(std::vector<E, A>& v, std::size_t n)
    requires std::is_default_constructible_v<E>
  {
    v.resize(n);
  }
};
template <class E, std::size_t N>
struct sequence_traits<std::array<E, N>> : indexed_sequence_traits<std::array<E, N>> {
  static constexpr bool fixed_size = true;
};
struct sequence_ops {
  const detail::type_ops& (*element)();
  std::size_t (*size)(const void*);
  const void* (*at)(const void*, std::size_t);
  void* (*at_mut)(void*, std::size_t){};
  void (*resize)(void*, std::size_t){};
  std::size_t (*max_size)(const void*){};
  bool fixed_size = false;
  guid (*identity)(const void*, std::size_t){};
  error (*replace)(void*, const std::function<error(void*, const detail::type_ops&)>&){};
};
namespace detail {
template <class T> const sequence_ops& sequence_ops_of() {
  using traits = sequence_traits<T>;
  using E = typename traits::element;
  static_assert(std::same_as<decltype(traits::at(std::declval<const T&>(), 0)), const E*>);
  static const sequence_ops table = [] {
    sequence_ops ops{&ops_of<E>,
                     [](const void* p) { return traits::size(*static_cast<const T*>(p)); },
                     [](const void* p, std::size_t i) -> const void* {
                       return traits::at(*static_cast<const T*>(p), i);
                     }};
    if constexpr (requires(T& v) {
                    { traits::at_mut(v, 0) } -> std::same_as<E*>;
                  })
      ops.at_mut = [](void* p, std::size_t i) -> void* {
        return traits::at_mut(*static_cast<T*>(p), i);
      };
    if constexpr (requires(T& v) { traits::resize(v, std::size_t{}); })
      ops.resize = [](void* p, std::size_t n) { traits::resize(*static_cast<T*>(p), n); };
    if constexpr (requires(const T& v) { traits::max_size(v); })
      ops.max_size = [](const void* p) { return traits::max_size(*static_cast<const T*>(p)); };
    if constexpr (sequence_identity_traits<T>::enabled)
      ops.identity = [](const void* p, std::size_t i) {
        return sequence_identity_traits<T>::identity(*traits::at(*static_cast<const T*>(p), i));
      };
    if constexpr (requires { traits::fixed_size; })
      ops.fixed_size = traits::fixed_size;
    if constexpr (requires(T& v, const std::vector<E>& values) {
                    { traits::replace(v, values) } -> std::same_as<error>;
                  })
      ops.replace = [](void* p, const std::function<error(void*, const type_ops&)>& initialize) {
        std::vector<E> candidate;
        auto status = initialize(&candidate, ops_of<std::vector<E>>());
        return status.failed() ? status : traits::replace(*static_cast<T*>(p), candidate);
      };
    return ops;
  }();
  return table;
}
}
}
