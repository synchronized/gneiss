// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#pragma once

#include "documents/commands/editor_command_history.hpp"
#include "documents/scene_edit_session.hpp"
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/core/base/guid.hpp>
#include "ui/viewport/transform_gizmo_math.hpp"

namespace gneiss::editor {

/** 单次 Gizmo 拖动的预览与历史收尾；目标身份不随当前选择变化。 */
class transform_gizmo_drag final {
public:
  [[nodiscard]] gneiss::error begin(scene_edit_session& session);
  [[nodiscard]] gneiss::error preview(scene_edit_session& session,
                                              const gizmo_matrix& matrix);
  /** 释放、切换选择或隐藏视图时提交已应用的预览；记录失败回滚原目标。 */
  [[nodiscard]] gneiss::error finish(scene_edit_session& session,
                                             editor_command_history& history);
  [[nodiscard]] bool is_active() const noexcept { return node_.is_valid(); }
  [[nodiscard]] bool matches_selection(const scene_edit_session& session) const noexcept {
    return is_active() && session.selection() == node_;
  }

private:
  entity_id node_;
  entity_id parent_;
  gneiss::guid uuid_;
  gneiss::guid instance_uuid_;
  gneiss::guid source_uuid_;
  gneiss::math::transform before_ = {};
  bool was_dirty_ = false;
  scene_edit_session::version before_version_, preview_version_;
};

} // namespace gneiss::editor
