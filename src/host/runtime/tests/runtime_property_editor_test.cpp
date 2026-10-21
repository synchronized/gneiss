// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/c_owner.hpp"
#include "runtime_property_editor.hpp"

#include <gneiss/engine/world.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace gneiss;

namespace {

#define GNEISS_TEST_CHECK(expression)                                                              \
  do {                                                                                             \
    if (!(expression)) {                                                                           \
      std::fprintf(stderr, "检查失败：%s:%d：%s\n", __FILE__, __LINE__, #expression);              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

bool test_write_and_revision() {
  gneiss::test_support::world_owner world;
  GNEISS_TEST_CHECK(([&] {
                      const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
                      return gneiss::result{gneiss_world_create(&desc, world.put())};
                    })() == gneiss::result::success);
  gneiss::entity_id entity;
  GNEISS_TEST_CHECK(([&](entity_id& out_entity) {
                      gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
                      const auto native_result =
                          gneiss_world_entity_create(world.get(), &native_entity);
                      if (native_result == GNEISS_SUCCESS) {
                        out_entity = entity_id{native_entity};
                      }
                      return from_native(native_result);
                    })(entity) == gneiss::result::success);
  gneiss::scene_node_id scene_node;
  GNEISS_TEST_CHECK(([&](scene_node_id parent, entity_id entity, scene_node_id& out_node) {
                      gneiss_scene_node_id native_node = GNEISS_NULL_SCENE_NODE_ID;
                      const auto native_result = gneiss_scene_node_create(
                          world.get(), parent.get(), entity.get(), &native_node);
                      if (native_result == GNEISS_SUCCESS) {
                        out_node = scene_node_id{native_node};
                      }
                      return from_native(native_result);
                    })({}, entity, scene_node) == gneiss::result::success);
  const gneiss::transform identity{};
  GNEISS_TEST_CHECK(([&](scene_node_id node, const gneiss::transform& value) {
                      const auto native = to_native(value);
                      return from_native(
                          gneiss_scene_node_set_local_transform(world.get(), node.get(), &native));
                    })(scene_node, identity) == gneiss::result::success);

  gneiss::host::runtime::runtime_scene_inspection inspection(7U);
  const std::vector<gneiss::host::runtime::runtime_scene_source_node> nodes{
      {.native_node = 1U,
       .native_parent = 0U,
       .native_entity = entity.get(),
       .uuid = "root",
       .prefab_instance_uuid = {},
       .prefab_source_node_uuid = {},
       .name = "Root",
       .local_transform = GNEISS_TRANSFORM_IDENTITY,
       .component_flags = 0U,
       .camera = GNEISS_CAMERA_DESC_INIT,
       .mesh_uri = {},
       .material_uri = {}}};
  gneiss::host::runtime::runtime_scene_snapshot snapshot;
  GNEISS_TEST_CHECK(inspection.capture(nodes, false, snapshot) == gneiss::result::success);

  gneiss::host::runtime::runtime_property_editor editor;
  GNEISS_TEST_CHECK(editor.initialize(world.get(), inspection, 7U) == gneiss::result::success);
  const gneiss::ipc_property_write command{.session_id = 7U,
                                           .command_id = 1U,
                                           .object = snapshot.changes.front().id,
                                           .type_id = gneiss_transform_type_id(),
                                           .field_id = GNEISS_TRANSFORM_FIELD_TRANSLATION,
                                           .expected_revision = 1U,
                                           .value = {std::array<float, 3>{2.0F, 3.0F, 4.0F}}};
  gneiss::ipc_property_write_result response;
  GNEISS_TEST_CHECK(editor.execute(command, response) == gneiss::result::success);
  if (response.code != GNEISS_SUCCESS) {
    std::fprintf(stderr, "写入失败：code=%d message=%s revision=%llu\n", response.code,
                 response.message.c_str(), static_cast<unsigned long long>(response.revision));
  }
  GNEISS_TEST_CHECK(response.code == GNEISS_SUCCESS);
  GNEISS_TEST_CHECK(response.revision == 2U);
  GNEISS_TEST_CHECK(response.canonical_value.payload == command.value.payload);
  gneiss::transform transform{};
  GNEISS_TEST_CHECK(([&](entity_id entity, gneiss::transform& output) {
                      gneiss_transform native{};
                      const auto status = from_native(gneiss_world_entity_get_local_transform(
                          world.get(), entity.get(), &native));
                      if (status.ok()) {
                        output = from_native(native);
                      }
                      return status;
                    })(entity, transform) == gneiss::result::success);
  GNEISS_TEST_CHECK(transform.translation[0] == 2.0F && transform.translation[1] == 3.0F &&
                    transform.translation[2] == 4.0F);

  auto stale = command;
  stale.command_id = 2U;
  response = {};
  GNEISS_TEST_CHECK(editor.execute(stale, response) == gneiss::result::success);
  GNEISS_TEST_CHECK(response.code == GNEISS_ERROR_INVALID_STATE && response.revision == 2U);
  GNEISS_TEST_CHECK(([&](entity_id entity, gneiss::transform& output) {
                      gneiss_transform native{};
                      const auto status = from_native(gneiss_world_entity_get_local_transform(
                          world.get(), entity.get(), &native));
                      if (status.ok()) {
                        output = from_native(native);
                      }
                      return status;
                    })(entity, transform) == gneiss::result::success);
  GNEISS_TEST_CHECK(transform.translation[0] == 2.0F && transform.translation[1] == 3.0F &&
                    transform.translation[2] == 4.0F);
  return true;
}

bool test_rejections() {
  gneiss::test_support::world_owner world;
  GNEISS_TEST_CHECK(([&] {
                      const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
                      return gneiss::result{gneiss_world_create(&desc, world.put())};
                    })() == gneiss::result::success);
  gneiss::host::runtime::runtime_scene_inspection inspection(9U);
  gneiss::host::runtime::runtime_property_editor editor;
  GNEISS_TEST_CHECK(editor.initialize(world.get(), inspection, 9U) == gneiss::result::success);
  const gneiss::ipc_property_write missing{.session_id = 9U,
                                           .command_id = 1U,
                                           .object = {1U, 1U},
                                           .type_id = gneiss_transform_type_id(),
                                           .field_id = GNEISS_TRANSFORM_FIELD_SCALE,
                                           .expected_revision = 1U,
                                           .value = {std::array<float, 3>{1.0F, 1.0F, 1.0F}}};
  gneiss::ipc_property_write_result response;
  GNEISS_TEST_CHECK(editor.execute(missing, response) == gneiss::result::success);
  GNEISS_TEST_CHECK(response.code == GNEISS_ERROR_NOT_FOUND);
  auto wrong_session = missing;
  wrong_session.session_id = 8U;
  return editor.execute(wrong_session, response) == gneiss::result::success &&
         response.code == GNEISS_ERROR_INVALID_STATE && response.session_id == 9U;
}

#undef GNEISS_TEST_CHECK

} // namespace

int main() { return test_write_and_revision() && test_rejections() ? 0 : 1; }
