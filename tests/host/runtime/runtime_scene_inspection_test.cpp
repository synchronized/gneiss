// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/async_application.hpp"
#include "../../support/movement_component.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/scene/scene_types.hpp"
#include "engine/function/meta_registration.hpp"
#include "runtime_property_editor.hpp"
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
          .components = {}};
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
  nodes[1].components = {
      gneiss::engine::camera_description{{1.04719755F, 0.25F, 1000.0F}},
      gneiss::engine::mesh_renderer_description{"asset:///meshes/child.gneiss-mesh",
                                                "asset:///materials/child.material.json"}};
  if (inspection.capture(nodes, false, snapshot) != gneiss::engine::error::success ||
      snapshot.is_full || snapshot.stamp.sequence != 2U || snapshot.changes.size() != 1U ||
      snapshot.changes[0].type != runtime_scene_change_type::upsert ||
      snapshot.changes[0].id != old_child_id || snapshot.changes[0].node.name != "Renamed" ||
      snapshot.changes[0]
              .node.components.get<gneiss::engine::camera_description>()
              ->settings.near != 0.25F ||
      snapshot.changes[0]
              .node.components.get<gneiss::engine::mesh_renderer_description>()
              ->mesh_uri !=
          nodes[1].components.get<gneiss::engine::mesh_renderer_description>()->mesh_uri ||
      snapshot.changes[0]
              .node.components.get<gneiss::engine::mesh_renderer_description>()
              ->material_uri !=
          nodes[1].components.get<gneiss::engine::mesh_renderer_description>()->material_uri) {
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

bool test_registered_component_inspection() {
  namespace e = gneiss::engine;
  using namespace gneiss::test_support;
  auto owner = e::create_native();
  if (!owner)
    return false;
  auto& world = **owner;
  auto node = world.object_create({}, {e::guid::from_bytes({7}), "Custom"}, {});
  if (!node || e::install_components({world, *node},
                                     {movement_description{3}, e::mesh_renderer_description{}})
                   .failed())
    return false;
  auto components = e::capture_components(world, *node);
  if (!components || components->size() != 2 || !components->get<e::mesh_renderer_description>())
    return false;
  auto dependencies = e::component_dependencies(*components);
  if (!dependencies || !dependencies->empty())
    return false;
  gneiss::host::runtime_scene_inspection inspection(83);
  auto source = make_node(node->get(), 0, e::guid::from_bytes({7}), "Custom");
  source.native_entity = entity(world, *node);
  source.components = *components;
  gneiss::host::ipc_inspection_batch batch;
  if (inspection.capture(std::span{&source, 1}, true, batch).failed())
    return false;
  std::vector<std::uint8_t> bytes;
  if (gneiss::host::encode_ipc_inspection_batch(batch, bytes).failed())
    return false;
  gneiss::host::ipc_inspection_batch decoded;
  if (gneiss::host::decode_ipc_inspection_batch(bytes, decoded).failed())
    return false;
  // 未注册类型必须拒绝，失败不替换已有完整快照。
  auto invalid = bytes;
  std::string json(invalid.begin(), invalid.end());
  const auto position = json.find(movement_description::type_guid.string());
  if (position == std::string::npos)
    return false;
  json.replace(position, 36, "ffffffff-ffff-ffff-ffff-ffffffffffff");
  invalid.assign(json.begin(), json.end());
  if (gneiss::host::decode_ipc_inspection_batch(invalid, decoded).ok())
    return false;
  const auto& received = decoded.changes.front().node.components;
  if (!received.get<movement_description>() || received.get<movement_description>()->speed != 3 ||
      !received.get<e::mesh_renderer_description>() ||
      !received.get<e::mesh_renderer_description>()->mesh_uri.empty())
    return false;
  auto reflected = received.find(movement_description::type_guid)->view();
  if (!reflected || !reflected->property("speed"))
    return false;
  gneiss::host::runtime_property_editor editor;
  if (editor.initialize(world, inspection, 83).failed())
    return false;
  gneiss::host::ipc_property_write command{.session_id = 83,
                                           .command_id = 1,
                                           .object = batch.changes.front().id,
                                           .type_id = movement_description::type_guid,
                                           .field_id = 1,
                                           .expected_revision = 1,
                                           .value = {6.0F}};
  gneiss::host::ipc_property_write_result response;
  if (editor.execute(command, response).failed() || response.code != e::error::success.code() ||
      state(world).get<movement_component>(source.native_entity)->data.speed != 6)
    return false;
  command.command_id = 2;
  command.expected_revision = 2;
  command.value = {-1.0F};
  if (editor.execute(command, response).failed() ||
      response.code != e::error::invalid_argument.code() ||
      state(world).get<movement_component>(source.native_entity)->data.speed != 6)
    return false;
  if (e::remove_component({world, *node}, e::mesh_renderer_description::type_guid).failed())
    return false;
  components = e::capture_components(world, *node);
  return components && !components->get<e::mesh_renderer_description>();
}

} // namespace

int main() {
  if (gneiss::engine::initialize_meta(gneiss::test_support::register_movement_meta).failed() ||
      gneiss::engine::register_scene_component(gneiss::test_support::operations()).failed())
    return 99;
  if (!test_registered_component_inspection())
    return 98;
  return test_full_and_incremental_snapshot() && test_validation_and_reset() &&
                 test_empty_scene_is_stable() && test_prefab_composite_identity() &&
                 test_capture_prefab_scene()
             ? 0
             : 1;
}
