// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "viewport/transform_gizmo_drag.hpp"
#include "documents/scene_document_edit.hpp"
#include "engine/core/error.hpp"

#include <algorithm>
#include <iterator>
#include <new>

namespace gneiss::editor {
namespace {

bool equal(const gneiss::math::transform& left,
           const gneiss::math::transform& right) noexcept {
  return left.translation == right.translation && left.rotation == right.rotation &&
         left.scale == right.scale;
}

} // namespace

gneiss::error transform_gizmo_drag::begin(scene_edit_session& session) {
  if (is_active()) {
    return gneiss::error::invalid_state;
  }
  const auto node = session.selected_node();
  const auto prefab = session.selected_prefab_node();
  if (!node && (!prefab || prefab->is_instance_root)) {
    return gneiss::error::not_ready;
  }
  {
    uuid_ = bool(node) ? node->uuid : gneiss::guid{};
    instance_uuid_ = !node ? prefab->instance_uuid : gneiss::guid{};
    source_uuid_ = !node ? prefab->source_node_uuid : gneiss::guid{};
    parent_ = bool(node) ? node->parent : prefab->parent;
    before_ = bool(node) ? node->local_transform : prefab->local_transform;
    was_dirty_ = session.is_dirty();
    before_version_ = preview_version_ = session.current_version();
    node_ = session.selection();
    return gneiss::error::success;
  }
}

gneiss::error transform_gizmo_drag::preview(scene_edit_session& session,
                                                    const gizmo_matrix& matrix) {
  if (!matches_selection(session) || session.current_version() != preview_version_) {
    return gneiss::error::invalid_state;
  }
  gneiss::math::transform target = {};
  auto operation = gizmo_matrix_to_transform(matrix, target);
  gneiss::math::transform parent = {};
  if (operation == gneiss::error::success && parent_.is_valid()) {
    operation = session.get_world_transform(parent_, parent);
  }
  gneiss::math::transform local = {};
  if (operation == gneiss::error::success) {
    operation = world_to_local_transform(parent_.is_valid() ? &parent : nullptr, target, local);
  }
  if (operation.ok()) {
    operation = session.set_local_transform(node_, local);
    if (operation.ok())
      preview_version_ = session.current_version();
  }
  return operation;
}

gneiss::error transform_gizmo_drag::finish(scene_edit_session& session,
                                                   editor_command_history& history) {
  if (!is_active()) {
    return gneiss::error::success;
  }
  const auto before_version = std::exchange(before_version_, {});
  const auto preview_version = std::exchange(preview_version_, {});
  const auto original_node = node_;
  node_ = {};
  const auto node = uuid_.is_nil() ? gneiss::error{gneiss::error::not_found}
                                   : session.find_node(uuid_);
  const auto prefab = source_uuid_.is_nil()
                          ? gneiss::error{gneiss::error::not_found}
                          : session.find_prefab_source(instance_uuid_, source_uuid_);
  // 场景已被替换或节点已删除时，不能向复用 UUID 的新对象提交旧拖动。
  if ((!node || node->node != original_node) && (!prefab || prefab->node != original_node)) {
    return gneiss::error::not_found;
  }
  if (session.current_version() != preview_version)
    return gneiss::error::invalid_state;
  const auto after = bool(node) ? node->local_transform : prefab->local_transform;
  if (equal(before_, after)) {
    if (session.current_version() != before_version)
      return session.restore_version(session.current_version(), before_version);
    return gneiss::error::success;
  }
  // 预览不直接成为历史；恢复编辑基线，再通过统一入口提交最终值。
  auto operation = session.restore_version(session.current_version(), before_version);
  if (operation.failed())
    return operation;
  {
    operation = execute_scene_edit(history, session, "变换节点", [&] {
      return session.set_local_transform(original_node, after);
    });
  }
  if (operation != gneiss::error::success) {
    const auto rollback = session.restore_version(session.current_version(), before_version);
    if (rollback != gneiss::error::success) {
      return rollback;
    }
    if (!was_dirty_) {
      session.clear_dirty();
    }
  }
  return operation;
}

} // namespace gneiss::editor
