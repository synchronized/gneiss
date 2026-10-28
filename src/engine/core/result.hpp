// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/error.hpp"
#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
namespace gneiss::engine {
/** 成功值或失败原因的互斥存储。布尔为真表示成功；解引用前必须确认成功。
 * 不在失败时构造 T，不进行堆分配；T/E 自身的构造与分配行为保持不变。 */
template <class T, class E = engine::error> class [[nodiscard]] result final {
  static_assert(!std::is_same_v<T, E> && !std::is_reference_v<T>);

public:
  result(const T& value)
    requires std::is_copy_constructible_v<T>
      : storage_(std::in_place_index<0>, value) {}
  result(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
      : storage_(std::in_place_index<0>, std::move(value)) {}
  result(E failure) : storage_(std::in_place_index<1>, std::move(failure)) {
    if constexpr (std::is_same_v<E, engine::error>) {
      assert(std::get<1>(storage_).failed());
      if (std::get<1>(storage_).ok())
        std::get<1>(storage_) = engine::error::internal;
    }
  }
  bool has_value() const noexcept { return storage_.index() == 0; }
  bool ok() const noexcept { return has_value(); }
  bool failed() const noexcept { return !has_value(); }
  explicit operator bool() const noexcept { return has_value(); }
  T& operator*() & {
    assert(has_value());
    return std::get<0>(storage_);
  }
  const T& operator*() const& {
    assert(has_value());
    return std::get<0>(storage_);
  }
  T&& operator*() && {
    assert(has_value());
    return std::get<0>(std::move(storage_));
  }
  T* operator->() { return std::addressof(**this); }
  const T* operator->() const { return std::addressof(**this); }
  E& error() & {
    assert(!has_value());
    return std::get<1>(storage_);
  }
  const E& error() const& {
    assert(!has_value());
    return std::get<1>(storage_);
  }
  E&& error() && {
    assert(!has_value());
    return std::get<1>(std::move(storage_));
  }

private:
  std::variant<T, E> storage_;
};
/** 无返回值时一般直接返回 error；泛型算法需要统一形态时使用 result<void>。 */
template <> class [[nodiscard]] result<void> final {
public:
  result() noexcept : status_(engine::error::success) {}
  result(engine::error status) noexcept : status_(status) {}
  bool has_value() const noexcept { return status_.ok(); }
  bool ok() const noexcept { return has_value(); }
  bool failed() const noexcept { return !has_value(); }
  explicit operator bool() const noexcept { return has_value(); }
  engine::error error() const noexcept {
    assert(failed());
    return status_;
  }

private:
  engine::error status_;
};
}
