// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/camera.hpp"
#include "engine/resource/material/material_resource.hpp"
#include "engine/resource/mesh/mesh_resource.hpp"

namespace gneiss::engine {
/** 相机参数为纯数据；活动相机只由 World 的实体 ID 决定。 */
struct camera_component {
  math::camera_settings value;
};
/** 组件直接保活运行资源，必须执行正常的构造、移动和析构。 */
struct mesh_renderer {
  engine::mesh mesh{};
  engine::material material{};
};
struct mesh_renderer_component {
  mesh_renderer value;
};
} // namespace gneiss::engine
