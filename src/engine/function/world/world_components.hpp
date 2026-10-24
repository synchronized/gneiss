// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "engine/core/math/camera_settings.hpp"
#include "engine/core/rid.hpp"

namespace gneiss::engine {
/** 相机参数为纯数据；活动相机只由 World 的实体 ID 决定。 */
struct camera_component {
  gneiss::engine::camera_settings value;
};
/** 组件只借用服务资源 RID；资产租约由场景实例持有。 */
struct mesh_renderer {
  gneiss::engine::rid mesh{};
  gneiss::engine::rid material{};
};
struct mesh_renderer_component {
  mesh_renderer value;
};
} // namespace gneiss::engine
