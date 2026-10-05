// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_RENDER_RENDER_SNAPSHOT_HPP_
#define GNEISS_RENDER_RENDER_SNAPSHOT_HPP_

#include "render/camera_math.h"
#include <cstdint>
#include <gneiss/render.h>
#include <gneiss/scene.h>
#include <vector>

namespace gneiss::render_internal {

/** Render 输入快照；只含值与 RID，不依赖 World、ECS 或场景节点状态。 */
struct render_camera_snapshot {
  gneiss_camera camera;
  gneiss_transform transform;
  render_internal::matrix4 view;
  render_internal::matrix4 projection;
  std::uint32_t viewport_width{};
  std::uint32_t viewport_height{};
};

struct render_instance_snapshot {
  gneiss_mesh mesh;
  gneiss_material material;
  gneiss_transform transform;
};

struct render_snapshot {
  render_camera_snapshot camera{};
  bool has_camera{};
  std::vector<render_instance_snapshot> instances;
};

} // namespace gneiss::render_internal

#endif
