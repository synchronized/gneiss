// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_CAMERA_MATH_HPP_
#define GNEISS_CORE_CAMERA_MATH_HPP_

#include "engine/core/math/camera_settings.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/result.hpp"

#include <array>

namespace gneiss::engine {

/** 使用 Camera 世界平移与旋转构造右手视图矩阵；Camera 缩放不参与观察变换。 */
[[nodiscard]] gneiss::engine::result build_view_matrix(const gneiss::engine::transform& transform,
                                                       matrix4& out_matrix) noexcept;

/** 构造右手透视投影矩阵，逻辑裁剪空间 Y 向上、深度范围为 0..1。 */
[[nodiscard]] gneiss::engine::result
build_perspective_matrix(const camera_settings& camera, float aspect, matrix4& out_matrix) noexcept;

/** 按 column 向量约定计算 matrix * vector。 */
[[nodiscard]] vector4 transform_vector(const matrix4& matrix, const vector4& vector) noexcept;

} // namespace gneiss::engine

#endif
