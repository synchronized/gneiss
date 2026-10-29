// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/async_application.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "runtime_scene_inspection.hpp"

#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include <string_view>
#include <thread>
#include <vector>

namespace {

gneiss::host::runtime_scene_source_node make_node(std::uint64_t node, std::uint64_t parent,
                                                  gneiss::engine::guid uuid, std::string name) {
  return {.native_node = node,
          .native_parent = parent,
          .native_entity = node,
          .uuid = std::move(uuid),
          .prefab_instance_uuid = {},
          .prefab_source_node_uuid = {},
          .name = std::move(name),
          .local_transform = {},
          .component_flags = 0U,
          .camera = {1.04719755F, 0.1F, 1000.0F},
          .mesh_uri = {},
          .material_uri = {}};
}

bool test_full_and_incremental_snapshot() {
  using namespace gneiss::host;
  runtime_scene_inspection inspection(77U);
  std::vector<runtime_scene_source_node> nodes{
      make_node(10U, 0U, *gneiss::engine::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf"),
                "Root"),
      make_node(20U, 10U, *gneiss::engine::guid::parse("0552e973-b75c-5936-a81b-c56bcd7688d5"),
                "Child")};
  runtime_scene_snapshot snapshot;
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::success ||
      !snapshot.is_full || snapshot.stamp.session_id != 77U || snapshot.stamp.sequence != 1U ||
      snapshot.changes.size() != 2U || snapshot.changes[0].node.parent.is_valid() ||
      snapshot.changes[1].node.parent != snapshot.changes[0].id) {
    return false;
  }
  const auto root_id = snapshot.changes[0].id;
  const auto old_child_id = snapshot.changes[1].id;
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::not_ready ||
      !snapshot.changes.empty()) {
    return false;
  }

  nodes[1].name = "Renamed";
  nodes[1].local_transform.translation[0] = 2.0F;
  nodes[1].component_flags =
      static_cast<std::uint32_t>(gneiss::scene_node_components::camera) |
      static_cast<std::uint32_t>(gneiss::scene_node_components::mesh_renderer);
  nodes[1].camera.near = 0.25F;
  nodes[1].mesh_uri = "asset:///meshes/child.gneiss-mesh";
  nodes[1].material_uri = "asset:///materials/child.material.json";
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::success ||
      snapshot.is_full || snapshot.stamp.sequence != 2U || snapshot.changes.size() != 1U ||
      snapshot.changes[0].type != runtime_scene_change_type::upsert ||
      snapshot.changes[0].id != old_child_id || snapshot.changes[0].node.name != "Renamed" ||
      snapshot.changes[0].node.camera.near != 0.25F ||
      snapshot.changes[0].node.mesh_uri != nodes[1].mesh_uri ||
      snapshot.changes[0].node.material_uri != nodes[1].material_uri) {
    return false;
  }

  nodes.erase(nodes.begin() + 1);
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::success ||
      snapshot.changes.size() != 1U ||
      snapshot.changes[0].type != runtime_scene_change_type::remove ||
      snapshot.changes[0].id != old_child_id) {
    return false;
  }
  nodes.push_back(make_node(30U, 10U,
                            *gneiss::engine::guid::parse("34c70a8f-c340-5fb3-9a78-64e4fd396cf0"),
                            "Replacement"));
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::success ||
      snapshot.changes.size() != 1U || snapshot.changes[0].id.value != old_child_id.value ||
      snapshot.changes[0].id.generation != old_child_id.generation + 1U ||
      snapshot.changes[0].node.parent != root_id) {
    return false;
  }
  return inspection.capture(nodes, true, snapshot) == gneiss::engine::error::success &&
         snapshot.is_full && snapshot.stamp.sequence == 5U && snapshot.changes.size() == 2U;
}

bool test_validation_and_reset() {
  using namespace gneiss::host;
  runtime_scene_inspection invalid(0U);
  runtime_scene_snapshot snapshot;
  std::vector<runtime_scene_source_node> nodes{make_node(
      1U, 0U, *gneiss::engine::guid::parse("f75c07c9-9e51-51b7-8dab-cffc2bf231c9"), "Node")};
  if (invalid.capture(nodes, false, snapshot) != gneiss::engine::error::invalid_argument) {
    return false;
  }
  runtime_scene_inspection inspection(1U);
  nodes.push_back(make_node(
      1U, 0U, *gneiss::engine::guid::parse("1b6731aa-61df-5a06-beca-f74909e7a766"), "Duplicate"));
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::invalid_argument) {
    return false;
  }
  nodes = {make_node(1U, 99U, *gneiss::engine::guid::parse("96eadd94-e998-5d37-9d62-c408d137eb09"),
                     "Orphan")};
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::invalid_argument) {
    return false;
  }
  inspection.reset(2U);
  nodes = {make_node(1U, 0U, *gneiss::engine::guid::parse("f75c07c9-9e51-51b7-8dab-cffc2bf231c9"),
                     "Node")};
  return inspection.capture(nodes, false, snapshot) == gneiss::engine::error::success &&
         snapshot.stamp.session_id == 2U && snapshot.stamp.sequence == 1U && snapshot.is_full;
}

bool test_empty_scene_is_stable() {
  gneiss::host::runtime_scene_inspection inspection(3U);
  gneiss::host::runtime_scene_snapshot snapshot;
  const std::vector<gneiss::host::runtime_scene_source_node> nodes;
  return inspection.capture(nodes, false, snapshot) == gneiss::engine::error::success &&
         snapshot.is_full && snapshot.stamp.sequence == 1U && snapshot.changes.empty() &&
         inspection.capture(nodes, false, snapshot) == gneiss::engine::error::not_ready;
}

bool test_prefab_composite_identity() {
  using namespace gneiss::host;
  runtime_scene_inspection inspection(4U);
  auto first = make_node(
      1U, 0U, *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c"), "First");
  first.prefab_instance_uuid = *gneiss::engine::guid::parse("6693b403-d7fb-5b73-ab3a-da0bbe718045");
  first.prefab_source_node_uuid =
      *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c");
  auto second = make_node(
      2U, 0U, *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c"), "Second");
  second.prefab_instance_uuid =
      *gneiss::engine::guid::parse("940a392c-a4eb-55a6-a13d-682e4ff6290b");
  second.prefab_source_node_uuid =
      *gneiss::engine::guid::parse("117a1bb4-e8b7-5da7-b737-9d7cf00fff3c");
  const std::vector<runtime_scene_source_node> nodes{first, second};
  runtime_scene_snapshot snapshot;
  return inspection.capture(nodes, false, snapshot) == gneiss::engine::error::success &&
         snapshot.changes.size() == 2U && snapshot.changes[0].id != snapshot.changes[1].id &&
         snapshot.changes[0].node.prefab_instance_uuid ==
             *gneiss::engine::guid::parse("6693b403-d7fb-5b73-ab3a-da0bbe718045") &&
         snapshot.changes[1].node.prefab_instance_uuid ==
             *gneiss::engine::guid::parse("940a392c-a4eb-55a6-a13d-682e4ff6290b");
}

bool test_capture_prefab_scene() {
  constexpr std::string_view asset_root = GNEISS_RUNTIME_TEST_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/prefab.scene.json";
  gneiss::runtime::application_configuration desc;
  desc.asset_root = asset_root;
  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::scene_instance_id scene = {};
  if (gneiss::engine::error{gneiss::runtime::application::create(desc, application)} !=
          gneiss::engine::error::success ||
      gneiss::engine::error{gneiss::test_support::load_scene(*application, scene_uri, &scene)} !=
          gneiss::engine::error::success) {
    return false;
  }
  gneiss::host::runtime_scene_inspection inspection(5U);
  gneiss::host::runtime_scene_snapshot snapshot;
  const auto capture = inspection.capture_scene(*application, scene, false, snapshot);
  const bool valid = capture == gneiss::engine::error::success && snapshot.changes.size() == 3U &&
                     snapshot.changes[1].node.prefab_instance_uuid ==
                         *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000012") &&
                     snapshot.changes[1].node.prefab_source_node_uuid ==
                         *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000004") &&
                     snapshot.changes[2].node.prefab_source_node_uuid ==
                         *gneiss::engine::guid::parse("30000000-0000-4000-8000-000000000002") &&
                     snapshot.changes[2].node.parent == snapshot.changes[1].node.id;
  auto native = application;
  auto foreign = gneiss::engine::error::success;
  const auto changes = snapshot.changes.size();
  std::thread worker([&] { foreign = inspection.capture_scene(*native, scene, true, snapshot); });
  worker.join();
  if (foreign != gneiss::engine::error::invalid_state || snapshot.changes.size() != changes)
    return false;
  const bool unloaded =
      gneiss::engine::error{application->scenes()->unload(scene)} == gneiss::engine::error::success;
  const bool destroyed =
      gneiss::engine::error{application->shutdown()} == gneiss::engine::error::success;
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
