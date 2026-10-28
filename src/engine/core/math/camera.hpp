// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/core/error.hpp"
#include "engine/core/math/transform.hpp"

// Win32 的旧式内存模型宏不能占用相机成员名。
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

namespace gneiss::engine::math {
/** 透视投影参数；是否作为活动相机由 World 决定。 */
struct camera_settings {
  float fov{};  ///< 垂直视场角，单位弧度。
  float near{}; ///< 近裁剪面距离。
  float far{};  ///< 远裁剪面距离。
};
/** 使用世界平移与旋转构造右手视图矩阵；缩放不参与观察变换；非法输入不修改 output。 */
[[nodiscard]] GNEISS_API error view_matrix_rh(const transform& value, matrix4& output) noexcept;
/** 右手透视投影；逻辑裁剪空间 Y 向上、深度范围为 0..1；非法输入不修改 output。 */
[[nodiscard]] GNEISS_API error perspective_rh_zo(const camera_settings& camera, float aspect,
                                                 matrix4& output) noexcept;
} // namespace gneiss::engine::math
