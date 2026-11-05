// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/guid.hpp"
#include "engine/core/math/types.hpp"
#include "engine/core/ref.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <variant>

namespace gneiss::engine {
namespace meta {
class object;
}
/** 值类别使用显式分组编号；与存储分支下标及文件标签无关。 */
enum class value_kind : std::uint32_t {
  invalid = 0,
  boolean = 1,
  int64 = 2,
  uint64 = 3,
  float32 = 4,
  float64 = 5,
  string = 6,
  guid = 7,
  // 数学值
  vec2 = 20,
  vec3 = 21,
  vec4 = 22,
  quaternion = 23,
  matrix3 = 24,
  matrix4 = 25,
  // 对象引用
  object = 50,
};
/** 拥有数据的通用值；复制字符串后不依赖源对象。对象分支仅保存
 * ref<meta::object>，复制共享对象，不隐式深拷贝；不承载任意结构体或裸指针。
 * 字符串构造和复制可能抛出分配异常，调用方在结果码边界转换错误。
 * 通用值不限制字符串编码或浮点范围；反射、文件和 IPC 各自执行边界校验。 */
struct value {
  static constexpr std::string_view type_name = "value";
  static constexpr engine::guid type_guid = engine::guid::from_bytes({0x97, 0x40});
  using storage_type =
      std::variant<std::monostate, bool, std::int64_t, std::uint64_t, float, double, std::string,
                   engine::guid, math::vector2, math::vector3, math::vector4, math::quaternion,
                   math::matrix3, math::matrix4, ref<meta::object>>;

  static constexpr std::array kind_index = {
      value_kind::invalid, value_kind::boolean, value_kind::int64,  value_kind::uint64,
      value_kind::float32, value_kind::float64, value_kind::string, value_kind::guid,
      value_kind::vec2,    value_kind::vec3,    value_kind::vec4,   value_kind::quaternion,
      value_kind::matrix3, value_kind::matrix4, value_kind::object};
  static_assert(kind_index.size() == std::variant_size_v<storage_type>);
  storage_type payload;
  /** 对象按指针身份比较；其他分支按类型及分量精确比较；不执行浮点容差或旋转等价比较。 */
  [[nodiscard]] bool operator==(const value&) const = default;
  [[nodiscard]] value_kind kind() const noexcept {
    return payload.valueless_by_exception() ? value_kind::invalid : kind_index[payload.index()];
  }
};
} // namespace gneiss::engine
