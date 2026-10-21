// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_SCENE_PROJECTION_MATH_HPP_
#define GNEISS_RENDER_SCENE_PROJECTION_MATH_HPP_

#include "engine/function/render/camera_math.hpp"

namespace gneiss::engine::render {

/** 从 Gneiss Transform 构造列主序 Model 与逆转置 Normal Matrix。 */
[[nodiscard]] bool build_model_matrices(const engine::core::math::transform& transform,
                                        engine::render::matrix4& model,
                                        engine::render::matrix4& normal) noexcept;

} // namespace gneiss::engine::render

#endif
