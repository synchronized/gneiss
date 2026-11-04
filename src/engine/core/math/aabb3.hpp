// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
#include "engine/core/math/functions.hpp"
#include <numeric>

namespace gneiss::engine::math {
/** 轴对齐闭包围盒；默认是原点处的有效退化盒，不代表空集合。
 * 有效边界必须有限且逐轴 min <= max；不使用隐藏的空盒哨兵。 */
struct aabb3 {
  vector3 min{};
  vector3 max{};

  [[nodiscard]] bool is_valid() const noexcept {
    return is_finite(min) && is_finite(max) && min.x <= max.x && min.y <= max.y && min.z <= max.z;
  }
  /** 要求有效边界；逐轴 midpoint 避免直接相加溢出。 */
  [[nodiscard]] vector3 center() const noexcept {
    return {std::midpoint(min.x, max.x), std::midpoint(min.y, max.y), std::midpoint(min.z, max.z)};
  }
  /** 要求有效边界；极大跨度仍可能超出 float 表示范围。 */
  [[nodiscard]] vector3 size() const noexcept { return max - min; }
  /** 包含边界；盒或点无效时返回 false。 */
  [[nodiscard]] bool contains(const vector3& point) const noexcept {
    return is_valid() && is_finite(point) && point.x >= min.x && point.x <= max.x &&
           point.y >= min.y && point.y <= max.y && point.z >= min.z && point.z <= max.z;
  }
  /** 接触边界也算相交；任一盒无效时返回 false。 */
  [[nodiscard]] bool intersects(const aabb3& other) const noexcept {
    return is_valid() && other.is_valid() && min.x <= other.max.x && max.x >= other.min.x &&
           min.y <= other.max.y && max.y >= other.min.y && min.z <= other.max.z &&
           max.z >= other.min.z;
  }
  /** 返回两盒的最小包围盒；调用方须保证两盒有效。 */
  [[nodiscard]] aabb3 merged(const aabb3& other) const noexcept {
    return {
        {std::min(min.x, other.min.x), std::min(min.y, other.min.y), std::min(min.z, other.min.z)},
        {std::max(max.x, other.max.x), std::max(max.y, other.max.y), std::max(max.z, other.max.z)}};
  }
  /** 扩展至包含指定点；调用方须保证当前盒有效且点有限。 */
  [[nodiscard]] aabb3 expanded(const vector3& point) const noexcept {
    return merged({point, point});
  }
};
} // namespace gneiss::engine::math
