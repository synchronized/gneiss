// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/math/export.hpp>

#include <gneiss/engine/core/math/transform.hpp>
#include <gneiss/engine/core/base/error.hpp>

namespace gneiss::math {
/** 透视投影参数；是否作为活动相机由应用选择。 */
struct GNEISS_MATH_API camera_settings {
  float fov{};        ///< 垂直视场角，单位弧度。
  float near_plane{}; ///< 近裁剪面距离。
  float far_plane{};  ///< 远裁剪面距离。

  /** 检查参数有限性、视场角范围及近远裁剪面顺序，不修改数据。 */
  [[nodiscard]] bool valid() const noexcept;
};
/** 使用世界平移与旋转构造右手视图矩阵；缩放不参与观察变换；非法输入不修改 output。 */
[[nodiscard]] GNEISS_MATH_API error view_matrix_rh(const transform& value,
                                                   matrix4& output) noexcept;
/** 右手透视投影；逻辑裁剪空间 Y 向上、深度范围为 0..1；非法输入不修改 output。 */
[[nodiscard]] GNEISS_MATH_API error perspective_rh_zo(const camera_settings& camera, float aspect,
                                                      matrix4& output) noexcept;
} // namespace gneiss::math
