// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_CAMERA_HPP_
#define GNEISS_RENDER_CAMERA_HPP_

#include <gneiss/engine/core/math/camera.hpp>
#include <cstdint>

namespace gneiss {

/** 由视口持有的相机输入；不引用 World 或场景实体。 */
struct view_camera {
  math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  math::transform transform{};
};

/** 相机矩阵与视口的纯值输入，不依赖 World、ECS 或资产对象。 */
struct render_camera_snapshot {
  math::camera_settings camera;
  math::transform transform;
  math::matrix4 view;
  math::matrix4 projection;
  std::uint32_t viewport_width{};
  std::uint32_t viewport_height{};
};

} // namespace gneiss

#endif
