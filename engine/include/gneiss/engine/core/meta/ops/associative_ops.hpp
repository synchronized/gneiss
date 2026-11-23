// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/meta/ops/ops_fwd.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gneiss::meta {
/** 当前只适配字符串键映射；size/keys/find 必需，修改操作可选。 */
template <class T> struct associative_traits {
  static constexpr bool enabled = false;
};
template <class T> struct string_map_traits {
  static constexpr bool enabled = true;
  using element = typename T::mapped_type;
  static std::size_t size(const T& v) { return v.size(); }
  static std::vector<std::string> keys(const T& v) {
    std::vector<std::string> out;
    for (const auto& entry : v)
      out.push_back(entry.first);
    return out;
  }
  static const element* find(const T& v, std::string_view key) {
    auto i = v.find(std::string(key));
    return i == v.end() ? nullptr : std::addressof(i->second);
  }
  static element* find(T& v, std::string_view key) {
    auto i = v.find(std::string(key));
    return i == v.end() ? nullptr : std::addressof(i->second);
  }
  static element* insert(T& v, std::string_view key)
    requires std::is_default_constructible_v<element>
  {
    return std::addressof(v.try_emplace(std::string(key)).first->second);
  }
  static bool erase(T& v, std::string_view key) { return v.erase(std::string(key)) != 0; }
  static void clear(T& v) { v.clear(); }
};
template <class V, class C, class A>
struct associative_traits<std::map<std::string, V, C, A>>
    : string_map_traits<std::map<std::string, V, C, A>> {};
template <class V, class H, class E, class A>
struct associative_traits<std::unordered_map<std::string, V, H, E, A>>
    : string_map_traits<std::unordered_map<std::string, V, H, E, A>> {};
struct associative_ops {
  const type_ops& (*element)();
  std::size_t (*size)(const void*);
  std::vector<std::string> (*keys)(const void*);
  const void* (*const_find)(const void*, std::string_view);
  void* (*find)(void*, std::string_view){};
  void* (*insert)(void*, std::string_view){};
  bool (*erase)(void*, std::string_view){};
  void (*clear)(void*){};
};
namespace detail {
template <class T> const associative_ops& associative_ops_of() {
  using traits = associative_traits<T>;
  using E = typename traits::element;
  static_assert(
      std::same_as<decltype(traits::find(std::declval<const T&>(), std::string_view{})), const E*>);
  static const associative_ops table = [] {
    associative_ops ops{&ops_of<E>,
                        [](const void* p) { return traits::size(*static_cast<const T*>(p)); },
                        [](const void* p) {
                          auto keys = traits::keys(*static_cast<const T*>(p));
                          std::ranges::sort(keys); // 自定义映射也保持确定性序列化顺序。
                          return keys;
                        },
                        [](const void* p, std::string_view key) -> const void* {
                          return traits::find(*static_cast<const T*>(p), key);
                        }};
    if constexpr (requires(T& v) {
                    { traits::find(v, std::string_view{}) } -> std::same_as<E*>;
                  })
      ops.find = [](void* p, std::string_view k) -> void* {
        return traits::find(*static_cast<T*>(p), k);
      };
    if constexpr (requires(T& v) {
                    { traits::insert(v, std::string_view{}) } -> std::same_as<E*>;
                  })
      ops.insert = [](void* p, std::string_view k) -> void* {
        return traits::insert(*static_cast<T*>(p), k);
      };
    if constexpr (requires(T& v) { traits::erase(v, std::string_view{}); })
      ops.erase = [](void* p, std::string_view k) { return traits::erase(*static_cast<T*>(p), k); };
    if constexpr (requires(T& v) { traits::clear(v); })
      ops.clear = [](void* p) { traits::clear(*static_cast<T*>(p)); };
    return ops;
  }();
  return table;
}
}
}
