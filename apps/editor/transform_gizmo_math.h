// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_TRANSFORM_GIZMO_MATH_H_
#define GNEISS_APPS_EDITOR_TRANSFORM_GIZMO_MATH_H_

#include <gneiss/scene.hpp>

namespace gneiss::editor {

/**
 * 将目标世界 TRS 转换为节点局部 TRS。
 *
 * parent_world 为空表示根节点。负缩放因矩阵 Gizmo 分解存在符号歧义而返回 unsupported。
 */
[[nodiscard]] result world_to_local_transform(const transform* parent_world,
                                              const transform& target_world,
                                              transform& output) noexcept;

} // namespace gneiss::editor

#endif
