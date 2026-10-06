// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "transform_gizmo_drag.hpp"

#include <gneiss/world.hpp>

struct ImVec2;

namespace gneiss::editor {

enum class transform_gizmo_operation { translate, rotate, scale };

/** 单次主线程绘制的借用上下文；所有引用必须在调用期间有效，不保存宿主状态。 */
struct transform_gizmo_context final {
  editor_session& session;
  editor_command_history& history;
  transform_gizmo_drag& drag;
  world_ref world;
  transform camera = GNEISS_TRANSFORM_IDENTITY;
  transform_gizmo_operation operation = transform_gizmo_operation::translate;
  bool& wait_release;
  result& error;
};

/** 要求当前 ImGui Context 已开始一帧。 */
void begin_transform_gizmo_frame() noexcept;
void reset_transform_gizmo() noexcept;
[[nodiscard]] bool draw_transform_gizmo(const transform_gizmo_context& context,
                                        const ImVec2& minimum, const ImVec2& size) noexcept;

} // namespace gneiss::editor
