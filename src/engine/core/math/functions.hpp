// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/math/types.hpp"
#include <algorithm>
#include <cmath>
namespace gneiss::engine {
template <class T> bool is_finite(const T& value) noexcept {
  return std::ranges::all_of(components(value), [](float v) { return std::isfinite(v); });
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
/** 按列向量约定计算 matrix * vector。 */
[[nodiscard]] inline vector4 transform_vector(const matrix4& matrix,
                                              const vector4& vector) noexcept {
  return matrix * vector;
}
}
