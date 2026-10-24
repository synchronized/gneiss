// SPDX-License-Identifier: MIT
// Copyright (c) 2026 CAORS contributors
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/detail/canonical_type_name.hpp"
#include <compare>
#include <cstddef>
#include <cstdint>
#include <gneiss/engine/core/export.h>
#include <string_view>
#include <type_traits>
namespace gneiss::engine {
/** Engine 存活期间的进程内类型键；零无效，不得序列化。 */
class runtime_type_id final {
public:
  constexpr runtime_type_id() noexcept = default;
  explicit constexpr runtime_type_id(std::uint64_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] explicit constexpr operator bool() const noexcept { return value_ != 0; }
  friend constexpr auto operator<=>(runtime_type_id, runtime_type_id) noexcept = default;

private:
  std::uint64_t value_{};
};
namespace detail {
/** 首次注册复制名称；共享 Engine 集中分配，不以名称 hash 作为身份。
 * 空名、分配失败及计数耗尽属于不可恢复的类型初始化错误。 */
[[nodiscard]] GNEISS_API runtime_type_id intern_runtime_type(const char* name,
                                                             std::size_t length) noexcept;
}
/** 相同工具链、ABI 和 ODR 定义的命名类型跨模块合并；去除顶层 cv/ref，void 返回无效 ID。
 * 匿名或局部类型不得作为跨模块契约。同名不同定义违反 ODR，不由注册表判断布局兼容性。 */
template <class T> [[nodiscard]] runtime_type_id runtime_type_of() noexcept {
  using type = std::remove_cvref_t<T>;
  if constexpr (std::is_void_v<type>)
    return {};
  else {
    constexpr auto name = detail::canonical_cpp_type_name<type>();
    static const auto id = detail::intern_runtime_type(name.data(), name.size());
    return id;
  }
}
/** 名称借用到 Engine 卸载；无效或未知 ID 返回空。线程安全。 */
[[nodiscard]] GNEISS_API std::string_view runtime_type_name(runtime_type_id id) noexcept;
}
