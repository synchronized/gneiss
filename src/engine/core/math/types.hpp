// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#pragma once
// 所有模块通过此入口使用相同配置，不允许改变数学对象布局。
#if defined(GLM_FORCE_LEFT_HANDED) || defined(GLM_FORCE_QUAT_DATA_WXYZ) ||                         \
    defined(GLM_FORCE_DEFAULT_ALIGNED_GENTYPES)
#error "Gneiss math configuration mismatch"
#endif
#define GLM_FORCE_CTOR_INIT
#define GLM_FORCE_SIZE_T_LENGTH
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <array>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
namespace gneiss::engine::math {
using vector2 = glm::vec<2, float, glm::packed_highp>;
using vector3 = glm::vec<3, float, glm::packed_highp>;
using vector4 = glm::vec<4, float, glm::packed_highp>;
using quaternion = glm::qua<float, glm::packed_highp>;
using matrix3 = glm::mat<3, 3, float, glm::packed_highp>;
using matrix4 = glm::mat<4, 4, float, glm::packed_highp>;
/** 文件和旧属性的 xyzw 顺序；与 GLM 的 wxyz 构造顺序分开。 */
[[nodiscard]] constexpr quaternion quaternion_xyzw(float x, float y, float z, float w) noexcept {
  return {w, x, y, z};
}
template <glm::length_t N> auto components(glm::vec<N, float, glm::packed_highp>& v) noexcept {
  return std::span<float, N>{glm::value_ptr(v), N};
}
template <glm::length_t N>
auto components(const glm::vec<N, float, glm::packed_highp>& v) noexcept {
  return std::span<const float, N>{glm::value_ptr(v), N};
}
inline auto components(quaternion& v) noexcept { return std::span<float, 4>{glm::value_ptr(v), 4}; }
inline auto components(const quaternion& v) noexcept {
  return std::span<const float, 4>{glm::value_ptr(v), 4};
}
inline auto matrix_elements(matrix4& v) noexcept {
  return std::span<float, 16>{glm::value_ptr(v), 16};
}
inline auto matrix_elements(const matrix4& v) noexcept {
  return std::span<const float, 16>{glm::value_ptr(v), 16};
}
template <class T, std::size_t N> auto components(std::array<T, N>& v) noexcept {
  return std::span<T, N>{v};
}
template <class T, std::size_t N> auto components(const std::array<T, N>& v) noexcept {
  return std::span<const T, N>{v};
}
/** 将数学分量复制到文件或 IPC 使用的固定数组，四元数保持 xyzw。 */
template <class T> auto component_array(const T& value) noexcept {
  const auto view = components(value);
  std::array<float, decltype(view)::extent> output{};
  for (std::size_t i = 0; i < output.size(); ++i)
    output[i] = view[i];
  return output;
}
static_assert(sizeof(vector3) == 3 * sizeof(float));
static_assert(sizeof(quaternion) == 4 * sizeof(float));
static_assert(sizeof(matrix4) == 16 * sizeof(float));
}
