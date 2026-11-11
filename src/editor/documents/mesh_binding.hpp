// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include "engine/resource/material.hpp"
#include "engine/resource/mesh.hpp"

namespace gneiss::editor {
/** 作者编辑绑定及撤销快照；持有稳定资源身份，不表示 GPU 已就绪。 */
struct scene_mesh_binding {
  engine::ref<const engine::mesh> mesh;
  engine::ref<const engine::material> material;
};

} // namespace gneiss::editor
