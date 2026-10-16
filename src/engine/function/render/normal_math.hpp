// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_NORMAL_MATH_HPP_
#define GNEISS_RENDER_NORMAL_MATH_HPP_

#include "engine/core/math/transform.hpp"

#include <array>

namespace gneiss::render_internal {

/** 使用 Transform 的逆转置缩放与旋转变换单位法线，并重新归一化。 */
[[nodiscard]] std::array<float, 3> transform_normal(const std::array<float, 3>& normal,
                                                    const engine::transform& transform) noexcept;

} // namespace gneiss::render_internal

#endif
