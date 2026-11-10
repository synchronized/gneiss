// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/camera.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/resource/material.hpp"
#include "engine/resource/mesh.hpp"

namespace gneiss::engine {
/** 相机参数为纯数据；默认相机由 camera_selection 独立选择。 */
struct camera_component {
  math::camera_settings value;
  bool primary_hint{}; // 应用选择默认相机的提示；安装组件不激活相机。
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
