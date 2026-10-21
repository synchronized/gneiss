// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/rid.hpp"
#include "engine/function/render/camera_settings.hpp"

namespace gneiss::engine::world {
/** 相机参数为纯数据；活动相机只由 World 的实体 ID 决定。 */
struct camera_component {
  engine::render::camera_settings value;
};
/** 组件只借用服务资源 RID；资产租约由场景实例持有。 */
struct mesh_renderer {
  engine::core::rid mesh{};
  engine::core::rid material{};
};
struct mesh_renderer_component {
  mesh_renderer value;
};
} // namespace gneiss::engine::world
