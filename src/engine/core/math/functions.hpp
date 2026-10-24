// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/math/transform.hpp"
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
namespace gneiss::engine {
template <class T> bool is_finite(const T& value) noexcept {
  return std::ranges::all_of(components(value), [](float v) { return std::isfinite(v); });
}
inline bool valid_transform(const transform& value) noexcept {
  return is_finite(value.translation) && is_finite(value.rotation) && is_finite(value.scale) &&
         std::abs(glm::length(value.rotation) - 1.0F) <= 1.0e-4F &&
         std::ranges::all_of(components(value.scale),
                             [](float v) { return std::abs(v) >= 1.0e-6F; });
}
/** 延续场景树的 TRS 分量组合语义，不新增剪切表示。 */
inline transform compose_transform(const transform& parent, const transform& local) noexcept {
  return {parent.translation + parent.rotation * (parent.scale * local.translation),
          parent.rotation * local.rotation, parent.scale * local.scale};
}
inline matrix4 transform_matrix(const transform& value) noexcept {
  return glm::translate(matrix4{1.0F}, value.translation) *
         glm::scale(glm::mat4_cast(value.rotation), value.scale);
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
}
