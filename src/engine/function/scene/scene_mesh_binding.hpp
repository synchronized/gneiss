// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/function/render/render_asset_loader.hpp"

namespace gneiss::engine {
/** 作者编辑绑定及撤销快照；持有稳定资源身份，不表示 GPU 已就绪。 */
struct scene_mesh_binding {
  gneiss::engine::mesh mesh;
  gneiss::engine::material material;
};

} // namespace gneiss::engine
