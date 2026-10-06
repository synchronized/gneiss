// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_BACKEND_GRANIT_SCENE_PROJECTION_MATH_HPP_
#define GNEISS_RENDER_BACKEND_GRANIT_SCENE_PROJECTION_MATH_HPP_

#include "engine/function/render/camera_math.hpp"

#include <gneiss/engine/scene.h>

namespace gneiss::render_internal {

/** 从 Gneiss Transform 构造列主序 Model 与逆转置 Normal Matrix。 */
[[nodiscard]] bool build_model_matrices(const gneiss_transform& transform,
                                        render_internal::matrix4& model,
                                        render_internal::matrix4& normal) noexcept;

} // namespace gneiss::render_internal

#endif
