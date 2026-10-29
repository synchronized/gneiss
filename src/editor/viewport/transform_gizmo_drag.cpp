// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "viewport/transform_gizmo_drag.hpp"
#include "engine/core/error.hpp"

#include <algorithm>
#include <iterator>
#include <new>

namespace gneiss::editor {
namespace {

bool equal(const gneiss::engine::math::transform& left,
           const gneiss::engine::math::transform& right) noexcept {
  return left.translation == right.translation && left.rotation == right.rotation &&
         left.scale == right.scale;
}

} // namespace

gneiss::engine::error transform_gizmo_drag::begin(scene_document& session) noexcept {
  if (is_active()) {
    return gneiss::engine::error::invalid_state;
  }
  const auto* node = session.selected_node();
  const auto* prefab = session.selected_prefab_node();
  if (node == nullptr && (prefab == nullptr || prefab->is_instance_root)) {
    return gneiss::engine::error::not_ready;
  }
  try {
    uuid_ = node != nullptr ? node->uuid : engine::guid{};
    instance_uuid_ = node == nullptr ? prefab->instance_uuid : engine::guid{};
    source_uuid_ = node == nullptr ? prefab->source_node_uuid : engine::guid{};
    parent_ = node != nullptr ? node->parent : prefab->parent;
    before_ = node != nullptr ? node->local_transform : prefab->local_transform;
    was_dirty_ = session.is_dirty();
    node_ = session.selection();
    return gneiss::engine::error::success;
  } catch (const std::bad_alloc&) {
    return gneiss::engine::error::out_of_memory;
  }
}

gneiss::engine::error transform_gizmo_drag::preview(scene_document& session,
                                                    const gizmo_matrix& matrix) noexcept {
  if (!matches_selection(session)) {
    return gneiss::engine::error::invalid_state;
  }
  gneiss::engine::math::transform target = {};
  auto operation = gizmo_matrix_to_transform(matrix, target);
  gneiss::engine::math::transform parent = {};
  if (operation == gneiss::engine::error::success && parent_.is_valid()) {
    operation = session.get_world_transform(parent_, parent);
  }
  gneiss::engine::math::transform local = {};
  if (operation == gneiss::engine::error::success) {
    operation = world_to_local_transform(parent_.is_valid() ? &parent : nullptr, target, local);
  }
  return operation == gneiss::engine::error::success ? session.set_local_transform(node_, local)
                                                     : operation;
}

gneiss::engine::error transform_gizmo_drag::finish(scene_document& session,
                                                   editor_command_history& history) noexcept {
  if (!is_active()) {
    return gneiss::engine::error::success;
  }
  const auto original_node = node_;
  node_ = {};
  const auto* node = uuid_.is_nil() ? nullptr : session.find_node(uuid_);
  const auto* prefab =
      source_uuid_.is_nil() ? nullptr : session.find_prefab_source(instance_uuid_, source_uuid_);
  // 场景已被替换或节点已删除时，不能向复用 UUID 的新对象提交旧拖动。
  if ((node == nullptr || node->node != original_node) &&
      (prefab == nullptr || prefab->node != original_node)) {
    return gneiss::engine::error::not_found;
  }
  const auto after = node != nullptr ? node->local_transform : prefab->local_transform;
  if (equal(before_, after)) {
    return gneiss::engine::error::success;
  }
  auto operation = gneiss::engine::error::success;
  try {
    const auto resolve = [&session, uuid = uuid_, instance = instance_uuid_,
                          source = source_uuid_](const gneiss::engine::math::transform& value) {
      const auto* target = uuid.is_nil() ? nullptr : session.find_node(uuid);
      const auto* prefab_target =
          source.is_nil() ? nullptr : session.find_prefab_source(instance, source);
      if (target != nullptr) {
        return session.set_local_transform(target->node, value);
      }
      const auto id = prefab_target != nullptr ? prefab_target->node : scene_node_id{};
      return id.is_valid() ? session.set_local_transform(id, value)
                           : gneiss::engine::error::not_found;
    };
    operation = history.record({
        .label = "变换节点",
        .undo = [resolve, before = before_] { return resolve(before); },
        .redo = [resolve, after] { return resolve(after); },
        .merge_key = {},
    });
  } catch (const std::bad_alloc&) {
    operation = gneiss::engine::error::out_of_memory;
  }
  if (operation != gneiss::engine::error::success) {
    const auto rollback = session.set_local_transform(original_node, before_);
    if (rollback != gneiss::engine::error::success) {
      return rollback;
    }
    if (!was_dirty_) {
      session.clear_dirty();
    }
  }
  return operation;
}

} // namespace gneiss::editor
