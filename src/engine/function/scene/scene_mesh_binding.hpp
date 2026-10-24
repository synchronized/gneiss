// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/function/render/render_asset_loader.hpp"

namespace gneiss::engine {
/** 持有已就绪 Mesh/Material 版本；不拥有 Scene，须在渲染服务释放前销毁。 */
struct scene_mesh_binding {
  gneiss::engine::render_asset_ref mesh;
  gneiss::engine::render_asset_ref material;
};

} // namespace gneiss::engine
