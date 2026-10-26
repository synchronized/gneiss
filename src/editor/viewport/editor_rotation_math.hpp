// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_ROTATION_MATH_H_
#define GNEISS_APPS_EDITOR_EDITOR_ROTATION_MATH_H_

#include "engine/core/meta/type_info.hpp"
#include "engine/core/result.hpp"

#include <array>

namespace gneiss::editor {

/** 将归一化四元数转换为 XYZ 欧拉角，输出单位为度。 */
[[nodiscard]] gneiss::engine::result
quaternion_to_euler_degrees(const gneiss::engine::quaternion& quaternion,
                            std::array<float, 3>& output) noexcept;

/** 将以度表示的 XYZ 欧拉角转换为归一化四元数。 */
[[nodiscard]] gneiss::engine::result
euler_degrees_to_quaternion(const std::array<float, 3>& euler,
                            gneiss::engine::quaternion& output) noexcept;

} // namespace gneiss::editor

#endif
