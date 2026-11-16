// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once
#include <gneiss/engine/resource/material.hpp>
#include <gneiss/engine/resource/mesh.hpp>

namespace gneiss::editor {
/** 作者编辑绑定及撤销快照；持有稳定资源身份，不表示 GPU 已就绪。 */
struct scene_mesh_binding {
  gneiss::ref<const gneiss::mesh> mesh;
  gneiss::ref<const gneiss::material> material;
};

} // namespace gneiss::editor
