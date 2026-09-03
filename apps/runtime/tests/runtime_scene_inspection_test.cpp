// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "runtime_scene_inspection.h"

#include <string_view>
#include <vector>

namespace {

gneiss::runtime_internal::runtime_scene_source_node
make_node(std::uint64_t node, std::uint64_t parent, std::string uuid, std::string name) {
  return {.native_node = node,
          .native_parent = parent,
          .native_entity = node,
          .uuid = std::move(uuid),
          .prefab_instance_uuid = {},
          .prefab_source_node_uuid = {},
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

bool test_prefab_composite_identity() {
  using namespace gneiss::runtime_internal;
  runtime_scene_inspection inspection(4U);
  auto first = make_node(1U, 0U, "source", "First");
  first.prefab_instance_uuid = "instance-a";
  first.prefab_source_node_uuid = "source";
  auto second = make_node(2U, 0U, "source", "Second");
  second.prefab_instance_uuid = "instance-b";
  second.prefab_source_node_uuid = "source";
  const std::vector<runtime_scene_source_node> nodes{first, second};
  runtime_scene_snapshot snapshot;
  return inspection.capture(nodes, false, snapshot) == gneiss::result::success &&
         snapshot.changes.size() == 2U && snapshot.changes[0].id != snapshot.changes[1].id &&
         snapshot.changes[0].node.prefab_instance_uuid == "instance-a" &&
         snapshot.changes[1].node.prefab_instance_uuid == "instance-b";
}

bool test_capture_prefab_scene() {
  constexpr std::string_view asset_root = GNEISS_RUNTIME_TEST_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss_application application = GNEISS_NULL_APPLICATION;
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  if (gneiss_application_create(&desc, &application) != GNEISS_SUCCESS ||
      gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS) {
    return false;
  }
  gneiss::runtime_internal::runtime_scene_inspection inspection(5U);
  gneiss::runtime_internal::runtime_scene_snapshot snapshot;
  const auto capture = inspection.capture_scene(application, scene, false, snapshot);
  const bool valid =
      capture == gneiss::result::success && snapshot.changes.size() == 3U &&
      snapshot.changes[1].node.prefab_instance_uuid == "30000000-0000-4000-8000-000000000012" &&
      snapshot.changes[1].node.prefab_source_node_uuid.empty() &&
      snapshot.changes[2].node.prefab_source_node_uuid == "30000000-0000-4000-8000-000000000002" &&
      snapshot.changes[2].node.parent == snapshot.changes[1].node.id;
  const bool unloaded = gneiss_scene_instance_unload(application, scene) == GNEISS_SUCCESS;
  const bool destroyed = gneiss_application_destroy(application) == GNEISS_SUCCESS;
  return valid && unloaded && destroyed;
}

} // namespace

int main() {
  return test_full_and_incremental_snapshot() && test_validation_and_reset() &&
                 test_empty_scene_is_stable() && test_prefab_composite_identity() &&
                 test_capture_prefab_scene()
             ? 0
             : 1;
}
