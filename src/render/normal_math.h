// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_NORMAL_MATH_H_
#define GNEISS_RENDER_NORMAL_MATH_H_

#include <gneiss/render.h>
#include <gneiss/scene.h>

#include <array>

namespace gneiss::render_internal {

/** 使用 Transform 的逆转置缩放与旋转变换单位法线，并重新归一化。 */
[[nodiscard]] std::array<float, 3> transform_normal(const gneiss_mesh_normal& normal,
                                                    const gneiss_transform& transform) noexcept;

} // namespace gneiss::render_internal

#endif
