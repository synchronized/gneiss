// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/core/types/guid.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include "inspection/runtime_property_editor.hpp"

#include <gneiss/engine/core/types/rid.hpp>

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
  gneiss::world_owner owner;

  GNEISS_TEST_CHECK(gneiss::test_support::assign_value(gneiss::create_native(), owner) ==
                    gneiss::error::success);
  auto object_scene = owner->create_scene();
  if (!object_scene)
    return false;
  auto object = object_scene->create_object({"object", {}});
  GNEISS_TEST_CHECK(object.has_value());
  auto entity = object->entity();
  gneiss::entity_id scene_node{entity.get()};
  const gneiss::math::transform identity{};
  GNEISS_TEST_CHECK(
      owner->find(gneiss::entity_id{(scene_node).get()}).set_local_transform(identity) ==
      gneiss::error::success);

  gneiss::host::runtime_scene_inspection inspection(7U);
  const std::vector<gneiss::host::runtime_scene_source_node> nodes{
      {.native_node = 1U,
       .native_parent = 0U,
       .native_entity = entity.get(),
       .uuid = *gneiss::guid::parse("21470280-22a3-5087-9443-62d27dfc4bbf"),
       .prefab_instance_uuid = {},
       .prefab_source_node_uuid = {},
       .name = "Root",
       .local_transform = {},
       .components = {}}};
  gneiss::host::runtime_scene_snapshot snapshot;
  GNEISS_TEST_CHECK(inspection.capture(nodes, false, snapshot) == gneiss::error::success);

  gneiss::host::runtime_property_editor editor;
  GNEISS_TEST_CHECK(editor.initialize(*owner, inspection, 7U) == gneiss::error::success);
  const gneiss::host::ipc_property_write command{
      .session_id = 7U,
      .command_id = 1U,
      .object = snapshot.changes.front().id,
      .type_id = gneiss::math::transform::type_guid,
      .field_id = gneiss::meta::transform_translation_field.get(),
      .expected_revision = 1U,
      .value = {gneiss::math::vector3{2.0F, 3.0F, 4.0F}}};
  gneiss::host::ipc_property_write_result response;
  GNEISS_TEST_CHECK(editor.execute(command, response) == gneiss::error::success);
  if (response.code != gneiss::error::success.code()) {
    std::fprintf(stderr, "写入失败：code=%d message=%s revision=%llu\n", response.code,
                 response.message.c_str(), static_cast<unsigned long long>(response.revision));
  }
  GNEISS_TEST_CHECK(response.code == gneiss::error::success.code());
  GNEISS_TEST_CHECK(response.revision == 2U);
  GNEISS_TEST_CHECK(response.canonical_value.payload == command.value.payload);
  gneiss::math::transform transform{};
  transform = owner->find(entity).local_transform();
  GNEISS_TEST_CHECK(transform.translation[0] == 2.0F && transform.translation[1] == 3.0F &&
                    transform.translation[2] == 4.0F);

  auto stale = command;
  stale.command_id = 2U;
  response = {};
  GNEISS_TEST_CHECK(editor.execute(stale, response) == gneiss::error::success);
  GNEISS_TEST_CHECK(response.code == gneiss::error::invalid_state.code() &&
                    response.revision == 2U);
  transform = owner->find(entity).local_transform();
  GNEISS_TEST_CHECK(transform.translation[0] == 2.0F && transform.translation[1] == 3.0F &&
                    transform.translation[2] == 4.0F);
  return true;
}

bool test_rejections() {
  gneiss::world_owner owner;

  GNEISS_TEST_CHECK(gneiss::test_support::assign_value(gneiss::create_native(), owner) ==
                    gneiss::error::success);
  gneiss::host::runtime_scene_inspection inspection(9U);
  gneiss::host::runtime_property_editor editor;
  GNEISS_TEST_CHECK(editor.initialize(*owner, inspection, 9U) == gneiss::error::success);
  const gneiss::host::ipc_property_write missing{
      .session_id = 9U,
      .command_id = 1U,
      .object = {1U, 1U},
      .type_id = gneiss::math::transform::type_guid,
      .field_id = gneiss::meta::transform_scale_field.get(),
      .expected_revision = 1U,
      .value = {gneiss::math::vector3{1.0F, 1.0F, 1.0F}}};
  gneiss::host::ipc_property_write_result response;
  GNEISS_TEST_CHECK(editor.execute(missing, response) == gneiss::error::success);
  GNEISS_TEST_CHECK(response.code == gneiss::error::not_found.code());
  auto wrong_session = missing;
  wrong_session.session_id = 8U;
  return editor.execute(wrong_session, response) == gneiss::error::success &&
         response.code == gneiss::error::invalid_state.code() && response.session_id == 9U;
}

#undef GNEISS_TEST_CHECK

} // namespace

int main() {
  if (gneiss::initialize_meta().failed())
    return 99;
  return test_write_and_revision() && test_rejections() ? 0 : 1;
}
