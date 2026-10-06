// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_drag.hpp"

#include <gneiss/engine/world.hpp>

#include <algorithm>
#include <iterator>
#include <new>

namespace gneiss::editor {
namespace {

bool equal(const transform& left, const transform& right) noexcept {
  return left.translation == right.translation && left.rotation == right.rotation &&
         left.scale == right.scale;
}

} // namespace

result transform_gizmo_drag::begin(editor_session& session) noexcept {
  if (is_active()) {
    return result::invalid_state;
  }
  const auto* node = session.selected_node();
  const auto* prefab = session.selected_prefab_node();
  if (node == nullptr && (prefab == nullptr || prefab->is_instance_root)) {
    return result::not_ready;
  }
  try {
    uuid_ = node != nullptr ? node->uuid : std::string{};
    instance_uuid_ = node == nullptr ? prefab->instance_uuid : std::string{};
    source_uuid_ = node == nullptr ? prefab->source_node_uuid : std::string{};
    parent_ = node != nullptr ? node->parent : prefab->parent;
    before_ = node != nullptr ? node->local_transform : prefab->local_transform;
    was_dirty_ = session.is_dirty();
    node_ = session.selection();
    return result::success;
  } catch (const std::bad_alloc&) {
    return result::out_of_memory;
  }
}

result transform_gizmo_drag::preview(editor_session& session, gneiss_world world,
                                     const gizmo_matrix& matrix) noexcept {
  if (!matches_selection(session)) {
    return result::invalid_state;
  }
  transform target = GNEISS_TRANSFORM_IDENTITY;
  auto operation = gizmo_matrix_to_transform(matrix, target);
  transform parent = GNEISS_TRANSFORM_IDENTITY;
  if (operation == result::success && parent_.is_valid()) {
    operation = world_ref{world}.get_world_transform(parent_, parent);
  }
  transform local = GNEISS_TRANSFORM_IDENTITY;
  if (operation == result::success) {
    operation = world_to_local_transform(parent_.is_valid() ? &parent : nullptr, target, local);
  }
  return operation == result::success ? session.set_local_transform(node_, local) : operation;
}

result transform_gizmo_drag::finish(editor_session& session,
                                    editor_command_history& history) noexcept {
  if (!is_active()) {
    return result::success;
  }
  const auto original_node = node_;
  node_ = {};
  const auto* node = uuid_.empty() ? nullptr : session.find_node(uuid_);
  const auto* prefab =
      source_uuid_.empty() ? nullptr : session.find_prefab_source(instance_uuid_, source_uuid_);
  // 场景已被替换或节点已删除时，不能向复用 UUID 的新对象提交旧拖动。
  if ((node == nullptr || node->node != original_node) &&
      (prefab == nullptr || prefab->node != original_node)) {
    return result::not_found;
  }
  const auto after = node != nullptr ? node->local_transform : prefab->local_transform;
  if (equal(before_, after)) {
    return result::success;
  }
  auto operation = result::success;
  try {
    const auto resolve = [&session, uuid = uuid_, instance = instance_uuid_,
                          source = source_uuid_](const transform& value) {
      const auto* target = uuid.empty() ? nullptr : session.find_node(uuid);
      const auto* prefab_target =
          source.empty() ? nullptr : session.find_prefab_source(instance, source);
      if (target != nullptr) {
        return session.set_local_transform(target->node, value);
      }
      const auto id = prefab_target != nullptr ? prefab_target->node : scene_node_id{};
      return id.is_valid() ? session.set_local_transform(id, value) : result::not_found;
    };
    operation = history.record({
        .label = "变换节点",
        .undo = [resolve, before = before_] { return resolve(before); },
        .redo = [resolve, after] { return resolve(after); },
        .merge_key = {},
    });
  } catch (const std::bad_alloc&) {
    operation = result::out_of_memory;
  }
  if (operation != result::success) {
    const auto rollback = session.set_local_transform(original_node, before_);
    if (rollback != result::success) {
      return rollback;
    }
    if (!was_dirty_) {
      session.clear_dirty();
    }
  }
  return operation;
}

} // namespace gneiss::editor
