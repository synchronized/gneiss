// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/camera.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/material.hpp"
#include "engine/resource/mesh.hpp"

namespace gneiss::engine {
/** World 内部维护的场景归属；随实体销毁，不对业务提供任意写入口。 */
struct scene_membership {
  scene_instance_id scene;
};
/** 相机参数为纯数据；活动相机只由 World 的实体 ID 决定。 */
struct camera_component {
  math::camera_settings value;
};
/** 组件直接保活运行资源，必须执行正常的构造、移动和析构。 */
struct mesh_renderer {
  ref<const engine::mesh> mesh{};
  ref<const engine::material> material{};
};
struct mesh_renderer_component {
  mesh_renderer value;
};
} // namespace gneiss::engine
