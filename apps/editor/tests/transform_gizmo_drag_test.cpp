// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "transform_gizmo_drag.hpp"

#include <gneiss/application.hpp>

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

bool near(const gneiss::transform& left, const gneiss::transform& right) {
  for (std::size_t index = 0; index < 3U; ++index) {
    if (std::abs(left.translation[index] - right.translation[index]) > 1.0e-4F ||
        std::abs(left.scale[index] - right.scale[index]) > 1.0e-4F) {
      return false;
    }
  }
  float dot = 0.0F;
  for (std::size_t index = 0; index < 4U; ++index) {
    dot += left.rotation[index] * right.rotation[index];
  }
  return std::abs(std::abs(dot) - 1.0F) < 1.0e-4F;
}

} // namespace

int main() try {
  using gneiss::result;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  const std::string root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  desc.asset_root = root.c_str();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  gneiss::application application;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::editor::editor_session session;
  if (gneiss::application::create_native(desc, application) != result::success ||
      application.get_world(world) != result::success ||
      session.open(application.get(), world, "asset://scenes/triangle.scene.json") !=
          result::success) {
    return 1;
  }
  const auto parent = session.nodes()[0].node;
  const auto child = session.nodes()[1].node;
  auto parent_transform = session.nodes()[0].local_transform;
  parent_transform.rotation[2] = 0.70710678F;
  parent_transform.rotation[3] = 0.70710678F;
  parent_transform.scale[0] = 2.0F;
  parent_transform.scale[1] = 3.0F;
  parent_transform.scale[2] = 4.0F;
  if (session.set_local_transform(parent, parent_transform) != result::success) {
    return 2;
  }
  // 同一生产拖动事务覆盖根节点、父子节点及三种操作；每种操作模拟多帧预览。
  for (const auto target : {child, parent}) {
    for (int mode = 0; mode < 3; ++mode) {
      gneiss::editor::editor_command_history history;
      gneiss::editor::transform_gizmo_drag drag;
      if (session.select(target) != result::success) {
        return 3;
      }
      const auto before = session.selected_node()->local_transform;
      if (drag.begin(session) != result::success) {
        return 4;
      }
      gneiss::transform desired{};
      if (gneiss::world_ref{world}
              .get_world_transform(gneiss::scene_node_id{target.get()}, desired)
              .native() != GNEISS_SUCCESS) {
        return 5;
      }
      for (int frame = 0; frame < 4; ++frame) {
        if (mode == 0) {
          desired.translation[0] += 1.0F;
        } else if (mode == 1) {
          const auto angle = 0.15F * static_cast<float>(frame + 1);
          desired.rotation[0] = 0.0F;
          desired.rotation[1] = std::sin(angle);
          desired.rotation[2] = 0.0F;
          desired.rotation[3] = std::cos(angle);
        } else {
          desired.scale[1] += 0.5F;
        }
        gneiss::editor::gizmo_matrix matrix{};
        gneiss::transform actual{};
        if (gneiss::editor::transform_to_gizmo_matrix(desired, matrix) != result::success ||
            drag.preview(session, world, matrix) != result::success ||
            gneiss::world_ref{world}
                    .get_world_transform(gneiss::scene_node_id{target.get()}, actual)
                    .native() != GNEISS_SUCCESS ||
            !near(actual, desired) || history.size() != 0U) {
          return 6;
        }
      }
      const auto after = session.selected_node()->local_transform;
      // 选择变化后预览必须被拒绝，但收尾仍记录原目标而非新选择。
      if (session.select(target == child ? parent : child) != result::success ||
          drag.matches_selection(session) ||
          drag.preview(session, world, {}) != result::invalid_state ||
          drag.finish(session, history) != result::success || history.size() != 1U ||
          drag.finish(session, history) != result::success || history.size() != 1U ||
          history.undo() != result::success || session.select(target) != result::success ||
          !near(session.selected_node()->local_transform, before) ||
          history.redo() != result::success ||
          !near(session.selected_node()->local_transform, after)) {
        return 7;
      }
    }
  }
  gneiss::editor::transform_gizmo_drag drag;
  gneiss::editor::editor_command_history unavailable_history(0U);
  session.clear_dirty();
  const auto before = session.selected_node()->local_transform;
  gneiss::transform desired = before;
  desired.translation[2] += 2.0F;
  gneiss::editor::gizmo_matrix matrix{};
  if (drag.begin(session) != result::success ||
      gneiss::editor::transform_to_gizmo_matrix(desired, matrix) != result::success ||
      drag.preview(session, world, matrix) != result::success ||
      drag.finish(session, unavailable_history) != result::invalid_argument ||
      !near(session.selected_node()->local_transform, before) || session.is_dirty()) {
    return 8;
  }
  if (drag.begin(session) != result::success) {
    return 9;
  }
  matrix[0] = std::numeric_limits<float>::quiet_NaN();
  gneiss::editor::editor_command_history history;
  if (drag.preview(session, world, matrix) != result::invalid_argument ||
      drag.finish(session, history) != result::success || history.size() != 0U ||
      session.is_dirty()) {
    return 10;
  }
  if (drag.begin(session) != result::success) {
    return 11;
  }
  session.close();
  if (drag.finish(session, history) != result::not_found || drag.is_active() ||
      history.size() != 0U) {
    return 12;
  }
  if (session.open(application.get(), world, "asset://scenes/prefab.scene.json") !=
          result::success ||
      session.select(session.prefab_nodes()[1].node) != result::success) {
    return 13;
  }
  const auto prefab_id = session.selection();
  const auto prefab_before = session.selected_prefab_node()->local_transform;
  if (gneiss::world_ref{world}
          .get_world_transform(gneiss::scene_node_id{prefab_id.get()}, desired)
          .native() != GNEISS_SUCCESS) {
    return 14;
  }
  desired.translation[0] += 3.0F;
  if (drag.begin(session) != result::success ||
      gneiss::editor::transform_to_gizmo_matrix(desired, matrix) != result::success ||
      drag.preview(session, world, matrix) != result::success ||
      session.select({}) != result::success || drag.matches_selection(session) ||
      drag.finish(session, history) != result::success || history.size() != 1U ||
      history.undo() != result::success || session.select(prefab_id) != result::success ||
      !near(session.selected_prefab_node()->local_transform, prefab_before) ||
      history.redo() != result::success ||
      near(session.selected_prefab_node()->local_transform, prefab_before)) {
    return 15;
  }
  history.clear();
  if (session.open(application.get(), world, "asset://scenes/triangle.scene.json") !=
          result::success ||
      session.select(session.nodes()[1].node) != result::success) {
    return 16;
  }
  const auto uuid = session.selected_node()->uuid;
  const auto deletion_before = session.selected_node()->local_transform;
  if (gneiss::world_ref{world}
          .get_world_transform(gneiss::scene_node_id{session.selection().get()}, desired)
          .native() != GNEISS_SUCCESS) {
    return 17;
  }
  desired.translation[0] += 2.0F;
  if (drag.begin(session) != result::success ||
      gneiss::editor::transform_to_gizmo_matrix(desired, matrix) != result::success ||
      drag.preview(session, world, matrix) != result::success ||
      drag.finish(session, history) != result::success) {
    return 18;
  }
  // Delete 在拖动收尾后创建独立命令；恢复节点的新句柄仍可通过 UUID 撤销此前拖动。
  gneiss::editor::scene_subtree_snapshot snapshot;
  if (session.destroy_subtree(session.selection(), snapshot) != result::success ||
      history.record({
          .label = "删除测试节点",
          .undo =
              [&session, snapshot] {
                gneiss::scene_node_id restored;
                const auto operation = session.restore_subtree(snapshot, restored);
                if (operation != result::success) {
                  std::printf("restore result=%d snapshot=%s\n", gneiss::to_native(operation),
                              snapshot.json.c_str());
                }
                return operation;
              },
          .redo =
              [&session, uuid] {
                const auto* current = session.find_node(uuid);
                if (current == nullptr) {
                  return result::not_found;
                }
                gneiss::editor::scene_subtree_snapshot discarded;
                return session.destroy_subtree(current->node, discarded);
              },
          .merge_key = {},
      }) != result::success) {
    return 19;
  }
  if (history.size() != 2U) {
    return 20;
  }
  if (history.undo() != result::success) {
    return 21;
  }
  if (history.undo() != result::success) {
    return 22;
  }
  if (session.find_node(uuid) == nullptr ||
      !near(session.find_node(uuid)->local_transform, deletion_before)) {
    return 23;
  }
  if (history.redo() != result::success) {
    return 24;
  }
  if (history.redo() != result::success) {
    return 25;
  }
  if (session.find_node(uuid) != nullptr) {
    return 26;
  }
  std::puts(
      "Gizmo: root/parent TRS, multi-frame, selection, undo/redo and failure recovery passed");
  return 0;
} catch (...) {
  return 99;
}
