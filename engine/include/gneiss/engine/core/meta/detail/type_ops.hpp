// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/meta/object.hpp>
#include <gneiss/engine/core/meta/ops/associative_ops.hpp>
#include <gneiss/engine/core/meta/ops/optional_ops.hpp>
#include <gneiss/engine/core/meta/ops/reference_ops.hpp>
#include <gneiss/engine/core/meta/ops/scalar_ops.hpp>
#include <gneiss/engine/core/meta/ops/sequence_ops.hpp>
#include <gneiss/engine/core/meta/ops/variant_ops.hpp>
#include <gneiss/engine/core/traits/type_identity.hpp>

#include <variant>

namespace gneiss::meta::detail {
using category_ops =
    std::variant<std::monostate, const scalar_ops*, const sequence_ops*, const associative_ops*,
                 const optional_ops*, const reference_ops*, const variant_ops*>;
/** 每个 C++ 类型共享的静态只读表；不持有实例，类别由 variant 唯一确定。 */
struct type_ops {
  bool enumeration = false;
  bool empty = false;
  gneiss::type_id id{};
  gneiss::guid guid{};
  void (*move_assign)(void*, void*){};
  void (*copy_assign)(void*, const void*){};
  category_ops category;
  template <class Ops> const Ops* get() const noexcept {
    const auto* p = std::get_if<const Ops*>(&category);
    return p ? *p : nullptr;
  }
  type_kind kind() const noexcept {
    if (empty)
      return type_kind::empty;
    return std::visit(
        []([[maybe_unused]] auto p) {
          using P = decltype(p);
          if constexpr (std::is_same_v<P, std::monostate>)
            return type_kind::object;
          else if constexpr (std::is_same_v<P, const scalar_ops*>)
            return type_kind::scalar;
          else if constexpr (std::is_same_v<P, const sequence_ops*>)
            return type_kind::sequence;
          else if constexpr (std::is_same_v<P, const associative_ops*>)
            return type_kind::associative;
          else if constexpr (std::is_same_v<P, const optional_ops*>)
            return type_kind::optional;
          else if constexpr (std::is_same_v<P, const reference_ops*>)
            return type_kind::reference;
          else
            return type_kind::variant;
        },
        category);
  }
  value_kind scalar_kind() const noexcept {
    auto* p = get<scalar_ops>();
    return p ? p->kind : value_kind::invalid;
  }
  const type_ops* element_ops() const {
    return std::visit(
        [](auto p) -> const type_ops* {
          using P = decltype(p);
          if constexpr (std::is_same_v<P, std::monostate> || std::is_same_v<P, const scalar_ops*> ||
                        std::is_same_v<P, const variant_ops*>)
            return nullptr;
          else
            return &p->element();
        },
        category);
  }
  const type_ops& element() const { return *element_ops(); }
};
/** 遍历全部叶类型；variant 的每个分支都必须满足注册约束。 */
template <class F>
error visit_type_leaves(const type_ops& access, const F& check, std::size_t depth = 0) {
  if (depth > 64)
    return error::invalid_argument;
  if (const auto* variant = access.get<variant_ops>()) {
    for (const auto* child : variant->alternatives) {
      auto status = visit_type_leaves(*child, check, depth + 1);
      if (status.failed())
        return status;
    }
    return error::success;
  }
  if (const auto* child = access.element_ops())
    return visit_type_leaves(*child, check, depth + 1);
  return check(access);
}

template <class T> const type_ops& ops_of() {
  static_assert(int(sequence_traits<T>::enabled) + int(associative_traits<T>::enabled) +
                        int(optional_traits<T>::enabled) + int(reference_traits<T>::enabled) +
                        int(variant_traits<T>::enabled) <=
                    1,
                "Conflicting reflection categories");
  static const type_ops table = [] {
    type_ops ops;
    ops.empty = std::is_same_v<T, std::monostate>;
    ops.id = runtime_type_of<T>();
    ops.guid = type_identity<scalar_storage<T>>::guid();
    ops.enumeration = std::is_enum_v<T>;
    if constexpr (std::is_copy_assignable_v<T>)
      ops.copy_assign = [](void* p, const void* v) {
        *static_cast<T*>(p) = *static_cast<const T*>(v);
      };
    if constexpr (std::is_move_assignable_v<T>)
      ops.move_assign = [](void* p, void* v) {
        *static_cast<T*>(p) = std::move(*static_cast<T*>(v));
      };
    if constexpr (reflected_value_kind<T>() != value_kind::invalid)
      ops.category = &scalar_ops_of<T>();
    else if constexpr (variant_traits<T>::enabled)
      ops.category = &variant_ops_of<T>();
    else if constexpr (sequence_traits<T>::enabled)
      ops.category = &sequence_ops_of<T>();
    else if constexpr (associative_traits<T>::enabled)
      ops.category = &associative_ops_of<T>();
    else if constexpr (optional_traits<T>::enabled)
      ops.category = &optional_ops_of<T>();
    else if constexpr (reference_traits<T>::enabled)
      ops.category = &reference_ops_of<T>();
    return ops;
  }();
  return table;
}
}
