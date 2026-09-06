// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_GRANIT_SCENE_PROJECTION_MATH_H_
#define GNEISS_RENDER_GRANIT_SCENE_PROJECTION_MATH_H_

#include "render/camera_math.h"

#include <gneiss/scene.h>

namespace gneiss::application_internal {

/** 从 Gneiss Transform 构造列主序 Model 与逆转置 Normal Matrix。 */
[[nodiscard]] bool build_model_matrices(const gneiss_transform& transform,
                                        render_internal::matrix4& model,
                                        render_internal::matrix4& normal) noexcept;

} // namespace gneiss::application_internal

#endif
