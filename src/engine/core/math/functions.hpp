// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/math/types.hpp"
#include <algorithm>
#include <cmath>
namespace gneiss::engine::math {
template <class T> bool is_finite(const T& value) noexcept {
  return std::ranges::all_of(components(value), [](float v) { return std::isfinite(v); });
}
/** 检查每个矩阵分量，包括非对角项。 */
template <glm::length_t Columns, glm::length_t Rows>
[[nodiscard]] bool
is_finite(const glm::mat<Columns, Rows, float, glm::packed_highp>& value) noexcept {
  for (glm::length_t column = 0; column < Columns; ++column)
    if (!is_finite(value[column]))
      return false;
  return true;
}
/** 仅用于明确为列主序的外部数组接口。 */
inline std::array<float, 16> matrix_to_array(const matrix4& value) noexcept {
  std::array<float, 16> output{};
  std::ranges::copy(matrix_elements(value), output.begin());
  return output;
}
inline matrix4 matrix_from_array(const std::array<float, 16>& value) noexcept {
  matrix4 output{0.0F};
  std::ranges::copy(value, matrix_elements(output).begin());
  return output;
}
/** 变换方向，使用 w=0，不应用平移，也不执行归一化或透视除法。 */
[[nodiscard]] inline vector3 transform_vector(const matrix4& matrix,
                                              const vector3& vector) noexcept {
  return vector3{matrix * vector4{vector, 0.0F}};
}
/** 变换位置，使用 w=1 并执行透视除法；w 为零或结果非有限时返回 false，保持输出。 */
[[nodiscard]] inline bool transform_point(const matrix4& matrix, const vector3& point,
                                          vector3& output) noexcept {
  const auto homogeneous = matrix * vector4{point, 1.0F};
  if (!is_finite(homogeneous) || homogeneous.w == 0.0F)
    return false;
  const auto candidate = vector3{homogeneous} / homogeneous.w;
  if (!is_finite(candidate))
    return false;
  output = candidate;
  return true;
}
} // namespace gneiss::engine::math
