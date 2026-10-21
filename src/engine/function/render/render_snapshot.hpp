// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_SNAPSHOT_HPP_
#define GNEISS_RENDER_RENDER_SNAPSHOT_HPP_

#include "engine/core/rid.hpp"
#include "engine/function/render/camera_math.hpp"
#include <cstdint>

#include <vector>

namespace gneiss::engine::render {

/** 由视口持有的相机输入；不引用 World 或场景实体。 */
struct view_camera {
  camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  engine::core::math::transform transform{};
};

/** Render 输入快照；只含值与 RID，不依赖 World、ECS 或场景节点状态。 */
struct render_camera_snapshot {
  camera_settings camera;
  engine::core::math::transform transform;
  engine::render::matrix4 view;
  engine::render::matrix4 projection;
  std::uint32_t viewport_width{};
  std::uint32_t viewport_height{};
};

struct render_instance_snapshot {
  engine::core::rid mesh;
  engine::core::rid material;
  engine::core::math::transform transform;
};

struct render_snapshot {
  render_camera_snapshot camera{};
  bool has_camera{};
  std::vector<render_instance_snapshot> instances;
};

} // namespace gneiss::engine::render

#endif
