// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_EDITOR_GRID_H_
#define GNEISS_APPS_EDITOR_EDITOR_GRID_H_

#include "transform_gizmo_math.hpp"
#include <gneiss/engine/render.hpp>
#include <vector>

namespace gneiss::editor {

/** 生成世界对齐的自适应地面网格；无效视口返回空列表，保留深度测试。 */
[[nodiscard]] std::vector<debug_line>
build_editor_grid(const transform& camera, const gizmo_matrix& view, float viewport_height);

} // namespace gneiss::editor
#endif
