// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/types.hpp"
#include <cstdint>
#include <string>
#include <variant>

namespace gneiss::engine {
/** 固定值类别；稳定 GUID 的类别编号延续既有属性值约定。 */
enum class value_kind : std::uint32_t {
  invalid = 0,
  boolean = 1,
  int64 = 2,
  uint64 = 3,
  float32 = 4,
  float64 = 5,
  string = 6,
  type_guid = 7,
  vec3 = 8,
  quaternion = 9,
};
/** 拥有数据的通用值；复制字符串后不依赖源对象。不承载任意 C++ 对象或借用指针。
 * 字符串构造和复制可能抛出分配异常，调用方在结果码边界转换错误。
 * 通用值不限制字符串编码或浮点范围；反射、文件和 IPC 各自执行边界校验。 */
struct value {
  using storage_type =
      std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
                   engine::guid, math::vector3, math::quaternion>;
  storage_type payload;
  [[nodiscard]] value_kind kind() const noexcept {
    return payload.valueless_by_exception() ? value_kind::invalid
                                            : static_cast<value_kind>(payload.index());
  }
};
} // namespace gneiss::engine
