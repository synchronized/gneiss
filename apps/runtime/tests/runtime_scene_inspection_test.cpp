// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_inspection.h"

#include <vector>

namespace {

gneiss::runtime_internal::runtime_scene_source_node
make_node(std::uint64_t node, std::uint64_t parent, std::string uuid, std::string name) {
  return {.native_node = node,
          .native_parent = parent,
          .native_entity = node,
          .uuid = std::move(uuid),
          .name = std::move(name),
          .local_transform = GNEISS_TRANSFORM_IDENTITY,
          .component_flags = 0U,
          .camera = GNEISS_CAMERA_DESC_INIT,
          .mesh_uri = {},
          .material_uri = {}};
}

bool test_full_and_incremental_snapshot() {
  using namespace gneiss::runtime_internal;
  runtime_scene_inspection inspection(77U);
  std::vector<runtime_scene_source_node> nodes{make_node(10U, 0U, "root", "Root"),
                                               make_node(20U, 10U, "child", "Child")};
  runtime_scene_snapshot snapshot;
  if (inspection.capture(nodes, false, snapshot) != gneiss::result::success || !snapshot.is_full ||
      snapshot.stamp.session_id != 77U || snapshot.stamp.sequence != 1U ||
      snapshot.changes.size() != 2U || snapshot.changes[0].node.parent.is_valid() ||
      snapshot.changes[1].node.parent != snapshot.changes[0].id) {
    return false;
  }
  const auto root_id = snapshot.changes[0].id;
  const auto old_child_id = snapshot.changes[1].id;
  if (inspection.capture(nodes, false, snapshot) != gneiss::result::not_ready ||
      !snapshot.changes.empty()) {
    return false;
  }

  nodes[1].name = "Renamed";
  nodes[1].local_transform.translation[0] = 2.0F;
  nodes[1].component_flags =
      GNEISS_SCENE_NODE_COMPONENT_CAMERA | GNEISS_SCENE_NODE_COMPONENT_MESH_RENDERER;
  nodes[1].camera.near_plane = 0.25F;
  nodes[1].mesh_uri = "asset:///meshes/child.gneiss-mesh";
  nodes[1].material_uri = "asset:///materials/child.material.json";
  if (inspection.capture(nodes, false, snapshot) != gneiss::result::success || snapshot.is_full ||
      snapshot.stamp.sequence != 2U || snapshot.changes.size() != 1U ||
      snapshot.changes[0].type != runtime_scene_change_type::upsert ||
      snapshot.changes[0].id != old_child_id || snapshot.changes[0].node.name != "Renamed" ||
      snapshot.changes[0].node.camera.near_plane != 0.25F ||
      snapshot.changes[0].node.mesh_uri != nodes[1].mesh_uri ||
      snapshot.changes[0].node.material_uri != nodes[1].material_uri) {
    return false;
  }

  nodes.erase(nodes.begin() + 1);
  if (inspection.capture(nodes, false, snapshot) != gneiss::result::success ||
      snapshot.changes.size() != 1U ||
      snapshot.changes[0].type != runtime_scene_change_type::remove ||
      snapshot.changes[0].id != old_child_id) {
    return false;
  }
  nodes.push_back(make_node(30U, 10U, "replacement", "Replacement"));
  if (inspection.capture(nodes, false, snapshot) != gneiss::result::success ||
      snapshot.changes.size() != 1U || snapshot.changes[0].id.value != old_child_id.value ||
      snapshot.changes[0].id.generation != old_child_id.generation + 1U ||
      snapshot.changes[0].node.parent != root_id) {
    return false;
  }
  return inspection.capture(nodes, true, snapshot) == gneiss::result::success && snapshot.is_full &&
         snapshot.stamp.sequence == 5U && snapshot.changes.size() == 2U;
}

bool test_validation_and_reset() {
  using namespace gneiss::runtime_internal;
  runtime_scene_inspection invalid(0U);
  runtime_scene_snapshot snapshot;
  std::vector<runtime_scene_source_node> nodes{make_node(1U, 0U, "node", "Node")};
  if (invalid.capture(nodes, false, snapshot) != gneiss::result::invalid_argument) {
    return false;
  }
  runtime_scene_inspection inspection(1U);
  nodes.push_back(make_node(1U, 0U, "duplicate-native", "Duplicate"));
  if (inspection.capture(nodes, false, snapshot) != gneiss::result::invalid_argument) {
    return false;
  }
  nodes = {make_node(1U, 99U, "orphan", "Orphan")};
  if (inspection.capture(nodes, false, snapshot) != gneiss::result::invalid_argument) {
    return false;
  }
  inspection.reset(2U);
  nodes = {make_node(1U, 0U, "node", "Node")};
  return inspection.capture(nodes, false, snapshot) == gneiss::result::success &&
         snapshot.stamp.session_id == 2U && snapshot.stamp.sequence == 1U && snapshot.is_full;
}

bool test_empty_scene_is_stable() {
  gneiss::runtime_internal::runtime_scene_inspection inspection(3U);
  gneiss::runtime_internal::runtime_scene_snapshot snapshot;
  const std::vector<gneiss::runtime_internal::runtime_scene_source_node> nodes;
  return inspection.capture(nodes, false, snapshot) == gneiss::result::success &&
         snapshot.is_full && snapshot.stamp.sequence == 1U && snapshot.changes.empty() &&
         inspection.capture(nodes, false, snapshot) == gneiss::result::not_ready;
}

} // namespace

int main() {
  return test_full_and_incremental_snapshot() && test_validation_and_reset() &&
                 test_empty_scene_is_stable()
             ? 0
             : 1;
}
