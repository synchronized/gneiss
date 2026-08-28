// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#ifndef GNEISS_APPS_EDITOR_TRANSFORM_GIZMO_MATH_H_
#define GNEISS_APPS_EDITOR_TRANSFORM_GIZMO_MATH_H_

#include <gneiss/scene.hpp>

#include <array>

namespace gneiss::editor {

using gizmo_matrix = std::array<float, 16>;

/** 按 column-major 布局把正缩放 TRS 转换为 Gizmo 模型矩阵。 */
[[nodiscard]] result transform_to_gizmo_matrix(const transform& value,
                                               gizmo_matrix& output) noexcept;

/** 从无剪切、正缩放的 Gizmo 模型矩阵恢复 TRS。 */
[[nodiscard]] result gizmo_matrix_to_transform(const gizmo_matrix& value,
                                               transform& output) noexcept;

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
