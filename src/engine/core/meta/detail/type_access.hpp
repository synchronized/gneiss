// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/meta/property.hpp"
#include "engine/core/result.hpp"
#include "engine/core/traits/type_identity.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
namespace gneiss::engine::meta {
/** 与属性读写共用的标量合法性规则。 */
GNEISS_API bool valid_scalar(const value&) noexcept;
namespace detail {
/** 静态类型操作表；仅借用真实成员/元素地址，不支持代理引用或临时 getter。 */
struct type_access {
  type_kind kind = type_kind::invalid;
  value_kind scalar = value_kind::invalid;
  engine::type_id id{};
  engine::guid guid{};
  const type_access& (*element)(){};
  value (*read)(const void*){};
  error (*write)(void*, const value&){};
  void (*assign)(void*, void*){};
  void (*copy_assign)(void*, const void*){};
  std::size_t (*size)(const void*){};
  const void* (*at)(const void*, std::size_t){};
  void* (*at_mut)(void*, std::size_t){};
  void (*resize)(void*, std::size_t){};
  std::vector<std::string> (*keys)(const void*){};
  const void* (*find)(const void*, std::string_view){};
  void* (*find_mut)(void*, std::string_view){};
  void* (*insert)(void*, std::string_view){};
  bool (*erase)(void*, std::string_view){};
  void (*clear)(void*){};
};
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
    std::is_integral_v<integer_base_t<T>> && !std::is_same_v<T, bool>,
    std::conditional_t<std::is_signed_v<integer_base_t<T>>, std::int64_t, std::uint64_t>, T>;
template <class T> value scalar_value(const T& input) {
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
  return static_cast<T>(*v);
}
template <class T, std::size_t Index = 1> constexpr value_kind reflected_value_kind() {
  if constexpr (Index == std::variant_size_v<value::storage_type>)
    return value_kind::invalid;
  else if constexpr (std::is_same_v<scalar_storage<T>,
                                    std::variant_alternative_t<Index, value::storage_type>>)
    return static_cast<value_kind>(Index);
  else
    return reflected_value_kind<T, Index + 1>();
}
template <class T> struct container_traits {
  static constexpr type_kind kind = type_kind::object;
};
template <class T, class A> struct container_traits<std::vector<T, A>> {
  static constexpr type_kind kind = type_kind::sequence;
  using element = T;
};
template <class T, std::size_t N> struct container_traits<std::array<T, N>> {
  static constexpr type_kind kind = type_kind::sequence;
  using element = T;
};
template <class T> struct container_traits<std::optional<T>> {
  static constexpr type_kind kind = type_kind::optional;
  using element = T;
};
template <class V, class C, class A> struct container_traits<std::map<std::string, V, C, A>> {
  static constexpr type_kind kind = type_kind::associative;
  using element = V;
};
template <class V, class H, class E, class A>
struct container_traits<std::unordered_map<std::string, V, H, E, A>> {
  static constexpr type_kind kind = type_kind::associative;
  using element = V;
};
template <class T> const type_access& access_of() {
  static const type_access table = [] {
    type_access a;
    a.id = runtime_type_of<T>();
    a.guid = type_identity<scalar_storage<T>>::guid();
    a.scalar = reflected_value_kind<T>();
    a.kind = a.scalar != value_kind::invalid ? type_kind::scalar : container_traits<T>::kind;
    if constexpr (std::is_copy_assignable_v<T>)
      a.copy_assign = [](void* target, const void* source) {
        *static_cast<T*>(target) = *static_cast<const T*>(source);
      };
    if constexpr (std::is_move_assignable_v<T>)
      a.assign = [](void* target, void* source) {
        *static_cast<T*>(target) = std::move(*static_cast<T*>(source));
      };
    if constexpr (reflected_value_kind<T>() != value_kind::invalid) {
      a.read = [](const void* p) { return scalar_value(*static_cast<const T*>(p)); };
      a.write = [](void* p, const value& v) -> error {
        auto decoded = scalar_cast<T>(v);
        if (!decoded)
          return decoded.error();
        *static_cast<T*>(p) = std::move(*decoded);
        return error::success;
      };
    } else if constexpr (container_traits<T>::kind != type_kind::object) {
      using E = typename container_traits<T>::element;
      static_assert(std::is_default_constructible_v<E>,
                    "Container elements require default construction");
      a.element = &access_of<E>;
      if constexpr (container_traits<T>::kind == type_kind::sequence) {
        static_assert(std::is_lvalue_reference_v<decltype(std::declval<T&>()[0])>,
                      "Proxy references are not supported");
        a.size = [](const void* p) { return static_cast<const T*>(p)->size(); };
        a.at = [](const void* p, std::size_t i) -> const void* {
          return std::addressof((*static_cast<const T*>(p))[i]);
        };
        a.at_mut = [](void* p, std::size_t i) -> void* {
          return std::addressof((*static_cast<T*>(p))[i]);
        };
        if constexpr (requires(T& v) { v.resize(std::size_t{}); })
          a.resize = [](void* p, std::size_t n) { static_cast<T*>(p)->resize(n); };
      } else if constexpr (container_traits<T>::kind == type_kind::optional) {
        static_assert(container_traits<E>::kind != type_kind::optional,
                      "Direct nested optional has ambiguous JSON null representation");
        a.size = [](const void* p) -> std::size_t {
          return static_cast<const T*>(p)->has_value() ? 1U : 0U;
        };
        a.at = [](const void* p, std::size_t) -> const void* {
          return std::addressof(**static_cast<const T*>(p));
        };
        a.at_mut = [](void* p, std::size_t) -> void* {
          return std::addressof(**static_cast<T*>(p));
        };
        a.resize = [](void* p, std::size_t n) {
          auto& v = *static_cast<T*>(p);
          if (!n)
            v.reset();
          else if (!v)
            v.emplace();
        };
      } else {
        a.size = [](const void* p) { return static_cast<const T*>(p)->size(); };
        a.keys = [](const void* p) {
          std::vector<std::string> keys;
          for (const auto& entry : *static_cast<const T*>(p))
            keys.push_back(entry.first);
          std::ranges::sort(keys); // 序列化顺序不依赖哈希表布局。
          return keys;
        };
        a.find = [](const void* p, std::string_view key) -> const void* {
          const auto& v = *static_cast<const T*>(p);
          auto it = v.find(std::string(key));
          return it == v.end() ? nullptr : std::addressof(it->second);
        };
        a.find_mut = [](void* p, std::string_view key) -> void* {
          auto& v = *static_cast<T*>(p);
          auto it = v.find(std::string(key));
          return it == v.end() ? nullptr : std::addressof(it->second);
        };
        a.insert = [](void* p, std::string_view key) -> void* {
          return std::addressof(static_cast<T*>(p)->try_emplace(std::string(key)).first->second);
        };
        a.erase = [](void* p, std::string_view key) {
          return static_cast<T*>(p)->erase(std::string(key)) != 0;
        };
        a.clear = [](void* p) { static_cast<T*>(p)->clear(); };
      }
    }
    return a;
  }();
  return table;
}
}
}
