// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include <gneiss/engine/core/math/camera.hpp>
#include <gneiss/engine/function/scene/scene_types.hpp>
#include <gneiss/engine/resource/material.hpp>
#include <gneiss/engine/resource/mesh.hpp>
#include <gneiss/engine/result.hpp>

namespace gneiss {
/** 相机参数为普通数据；调用方使用 settings.valid() 校验，修改后显式通知对象。
 * 默认构造为有效透视相机，活动相机选择独立于组件。 */
struct camera_component {
  math::camera_settings settings{1.04719755F, 0.1F, 1000.0F};
  bool primary_hint{}; // 默认相机选择提示，不自动激活。
};

/** 组件直接保活运行资源，必须执行正常的构造、移动和析构。 */
struct mesh_component {
  ref<const gneiss::mesh> mesh{};
  ref<const gneiss::material> material{};
};
} // namespace gneiss
