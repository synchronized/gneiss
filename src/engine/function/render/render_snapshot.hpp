// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_SNAPSHOT_HPP_
#define GNEISS_RENDER_RENDER_SNAPSHOT_HPP_

#include "engine/core/math/camera.hpp"
#include "engine/function/render/render_resource.hpp"
#include <cstdint>

#include <vector>

namespace gneiss::engine {

/** 由视口持有的相机输入；不引用 World 或场景实体。 */
struct view_camera {
  math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  math::transform transform{};
};

/** Render 输入快照；包含值与资源强引用，不依赖 World、ECS 或场景节点状态。 */
struct render_camera_snapshot {
  math::camera_settings camera;
  math::transform transform;
  math::matrix4 view;
  math::matrix4 projection;
  std::uint32_t viewport_width{};
  std::uint32_t viewport_height{};
};

struct render_instance_snapshot {
  engine::mesh mesh;
  engine::material material;
  math::transform transform;
};

struct render_snapshot {
  render_camera_snapshot camera{};
  bool has_camera{};
  std::vector<render_instance_snapshot> instances;
};

} // namespace gneiss::engine

#endif
