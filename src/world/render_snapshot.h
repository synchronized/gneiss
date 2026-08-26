// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_WORLD_RENDER_SNAPSHOT_H_
#define GNEISS_WORLD_RENDER_SNAPSHOT_H_

#include <gneiss/render.h>
#include <gneiss/scene.h>

#include <vector>

namespace gneiss::world_internal {

class world_state;

struct render_camera_snapshot {
  gneiss_camera camera;
  gneiss_transform transform;
};

struct render_instance_snapshot {
  gneiss_mesh mesh;
  gneiss_material material;
  gneiss_transform transform;
};

struct render_snapshot {
  render_camera_snapshot camera{GNEISS_CAMERA_INIT, GNEISS_TRANSFORM_IDENTITY};
  bool has_camera{};
  std::vector<render_instance_snapshot> instances;
};

[[nodiscard]] gneiss_result build_render_snapshot(world_state& world,
                                                  render_snapshot& out_snapshot) noexcept;
[[nodiscard]] gneiss_result get_render_snapshot(gneiss_world world,
                                                render_snapshot& out_snapshot) noexcept;

} // namespace gneiss::world_internal

#endif
