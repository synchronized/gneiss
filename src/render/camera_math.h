// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_CAMERA_MATH_H_
#define GNEISS_RENDER_CAMERA_MATH_H_

#include <gneiss/core/result.h>
#include <gneiss/render.h>
#include <gneiss/scene.h>

#include <array>

namespace gneiss::render_internal {

/** 列主序 4x4 矩阵，使用列向量和右乘组合。 */
struct matrix4 {
  std::array<float, 16> values{};
};

/** 使用 Camera 世界平移与旋转构造右手视图矩阵；Camera 缩放不参与观察变换。 */
[[nodiscard]] gneiss_result build_view_matrix(const gneiss_transform& transform,
                                              matrix4& out_matrix) noexcept;

/** 构造适配 Vulkan 深度范围和帧缓冲 Y 方向的右手透视投影矩阵。 */
[[nodiscard]] gneiss_result build_vulkan_perspective_matrix(const gneiss_camera& camera,
                                                            float aspect,
                                                            matrix4& out_matrix) noexcept;

/** 按 column 向量约定计算 matrix * vector。 */
[[nodiscard]] std::array<float, 4> transform_vector(const matrix4& matrix,
                                                    const std::array<float, 4>& vector) noexcept;

} // namespace gneiss::render_internal

#endif
