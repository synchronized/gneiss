// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "runtime_property_editor.hpp"

#include "engine/core/rid.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace gneiss::host;

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
  gneiss::engine::world_owner owner;

  GNEISS_TEST_CHECK(gneiss::engine::create_native(owner) == gneiss::engine::result::success);
  gneiss::entity_id entity;
  GNEISS_TEST_CHECK(owner->entity_create(entity) == gneiss::engine::result::success);
  gneiss::scene_node_id scene_node;
  GNEISS_TEST_CHECK(owner->node_create({}, entity, scene_node) == gneiss::engine::result::success);
  const gneiss::engine::transform identity{};
  GNEISS_TEST_CHECK(owner->node_set_local_transform(scene_node, identity) ==
                    gneiss::engine::result::success);

  gneiss::host::runtime_scene_inspection inspection(7U);
  const std::vector<gneiss::host::runtime_scene_source_node> nodes{
      {.native_node = 1U,
       .native_parent = 0U,
       .native_entity = entity.get(),
       .uuid = "root",
       .prefab_instance_uuid = {},
       .prefab_source_node_uuid = {},
       .name = "Root",
       .local_transform = {},
       .component_flags = 0U,
       .camera = {1.04719755F, 0.1F, 1000.0F},
       .mesh_uri = {},
       .material_uri = {}}};
  gneiss::host::runtime_scene_snapshot snapshot;
  GNEISS_TEST_CHECK(inspection.capture(nodes, false, snapshot) == gneiss::engine::result::success);

  gneiss::host::runtime_property_editor editor;
  GNEISS_TEST_CHECK(editor.initialize(*owner, inspection, 7U) == gneiss::engine::result::success);
  const gneiss::host::ipc_property_write command{
      .session_id = 7U,
      .command_id = 1U,
      .object = snapshot.changes.front().id,
      .type_id = gneiss::engine::transform_component_type,
      .field_id = gneiss::engine::transform_translation_field.get(),
      .expected_revision = 1U,
      .value = {std::array<float, 3>{2.0F, 3.0F, 4.0F}}};
  gneiss::host::ipc_property_write_result response;
  GNEISS_TEST_CHECK(editor.execute(command, response) == gneiss::engine::result::success);
  if (response.code != gneiss::engine::result::success.code()) {
    std::fprintf(stderr, "写入失败：code=%d message=%s revision=%llu\n", response.code,
                 response.message.c_str(), static_cast<unsigned long long>(response.revision));
  }
  GNEISS_TEST_CHECK(response.code == gneiss::engine::result::success.code());
  GNEISS_TEST_CHECK(response.revision == 2U);
  GNEISS_TEST_CHECK(response.canonical_value.payload == command.value.payload);
  gneiss::engine::transform transform{};
  GNEISS_TEST_CHECK(owner->entity_get_local_transform(entity, transform) ==
                    gneiss::engine::result::success);
  GNEISS_TEST_CHECK(transform.translation[0] == 2.0F && transform.translation[1] == 3.0F &&
                    transform.translation[2] == 4.0F);

  auto stale = command;
  stale.command_id = 2U;
  response = {};
  GNEISS_TEST_CHECK(editor.execute(stale, response) == gneiss::engine::result::success);
  GNEISS_TEST_CHECK(response.code == gneiss::engine::result::invalid_state.code() &&
                    response.revision == 2U);
  GNEISS_TEST_CHECK(owner->entity_get_local_transform(entity, transform) ==
                    gneiss::engine::result::success);
  GNEISS_TEST_CHECK(transform.translation[0] == 2.0F && transform.translation[1] == 3.0F &&
                    transform.translation[2] == 4.0F);
  return true;
}

bool test_rejections() {
  gneiss::engine::world_owner owner;

  GNEISS_TEST_CHECK(gneiss::engine::create_native(owner) == gneiss::engine::result::success);
  gneiss::host::runtime_scene_inspection inspection(9U);
  gneiss::host::runtime_property_editor editor;
  GNEISS_TEST_CHECK(editor.initialize(*owner, inspection, 9U) == gneiss::engine::result::success);
  const gneiss::host::ipc_property_write missing{
      .session_id = 9U,
      .command_id = 1U,
      .object = {1U, 1U},
      .type_id = gneiss::engine::transform_component_type,
      .field_id = gneiss::engine::transform_scale_field.get(),
      .expected_revision = 1U,
      .value = {std::array<float, 3>{1.0F, 1.0F, 1.0F}}};
  gneiss::host::ipc_property_write_result response;
  GNEISS_TEST_CHECK(editor.execute(missing, response) == gneiss::engine::result::success);
  GNEISS_TEST_CHECK(response.code == gneiss::engine::result::not_found.code());
  auto wrong_session = missing;
  wrong_session.session_id = 8U;
  return editor.execute(wrong_session, response) == gneiss::engine::result::success &&
         response.code == gneiss::engine::result::invalid_state.code() && response.session_id == 9U;
}

#undef GNEISS_TEST_CHECK

} // namespace

int main() { return test_write_and_revision() && test_rejections() ? 0 : 1; }
