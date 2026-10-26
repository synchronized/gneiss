// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_CORE_SCENE_PROJECTION_MATH_HPP_
#define GNEISS_CORE_SCENE_PROJECTION_MATH_HPP_

#include "engine/core/math/transform.hpp"

namespace gneiss::engine {

/** 从 Gneiss Transform 构造列主序 Model 与逆转置 Normal Matrix。 */
[[nodiscard]] bool build_model_matrices(const gneiss::engine::transform& transform,
                                        gneiss::engine::matrix4& model,
                                        gneiss::engine::matrix4& normal) noexcept;

} // namespace gneiss::engine

#endif
