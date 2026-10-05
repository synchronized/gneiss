// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "editor_command_history.hpp"
#include "editor_session.h"
#include "transform_gizmo_math.hpp"

namespace gneiss::editor {

/** 单次 Gizmo 拖动的预览与历史收尾；目标身份不随当前选择变化。 */
class transform_gizmo_drag final {
public:
  [[nodiscard]] result begin(editor_session& session) noexcept;
  [[nodiscard]] result preview(editor_session& session, gneiss_world world,
                               const gizmo_matrix& matrix) noexcept;
  /** 释放、切换选择或隐藏视图时提交已应用的预览；记录失败回滚原目标。 */
  [[nodiscard]] result finish(editor_session& session, editor_command_history& history) noexcept;
  [[nodiscard]] bool is_active() const noexcept { return node_.is_valid(); }
  [[nodiscard]] bool matches_selection(const editor_session& session) const noexcept {
    return is_active() && session.selection() == node_;
  }

private:
  scene_node_id node_;
  scene_node_id parent_;
  std::string uuid_;
  std::string instance_uuid_;
  std::string source_uuid_;
  transform before_ = GNEISS_TRANSFORM_IDENTITY;
  bool was_dirty_ = false;
};

} // namespace gneiss::editor
