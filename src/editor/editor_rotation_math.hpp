// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_ROTATION_MATH_H_
#define GNEISS_APPS_EDITOR_EDITOR_ROTATION_MATH_H_

#include <gneiss/core/result.hpp>
#include <gneiss/reflection.h>

#include <array>

namespace gneiss::editor {

/** 将归一化四元数转换为 XYZ 欧拉角，输出单位为度。 */
[[nodiscard]] result quaternion_to_euler_degrees(const gneiss_property_quaternion& quaternion,
                                                 std::array<float, 3>& output) noexcept;

/** 将以度表示的 XYZ 欧拉角转换为归一化四元数。 */
[[nodiscard]] result euler_degrees_to_quaternion(const std::array<float, 3>& euler,
                                                 gneiss_property_quaternion& output) noexcept;

} // namespace gneiss::editor

#endif
