// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "property_inspector_model.h"

#include <cmath>

namespace {

[[nodiscard]] bool nearly_equal(float left, float right) noexcept {
  return std::abs(left - right) < 0.0001F;
}

} // namespace

int main() {
  gneiss::world world;
  gneiss::entity_id entity;
  gneiss::entity_id transform_only;
  gneiss::scene_node_id entity_node;
  gneiss::scene_node_id transform_only_node;
  gneiss::editor::property_inspector_model inspector;
  if (gneiss::world::create(world) != gneiss::result::success ||
      world.create_entity(entity) != gneiss::result::success ||
      world.create_entity(transform_only) != gneiss::result::success ||
      inspector.initialize() != gneiss::result::success) {
    return 1;
  }
  gneiss::camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  if (world.create_scene_node({}, entity, entity_node) != gneiss::result::success ||
      world.create_scene_node({}, transform_only, transform_only_node) != gneiss::result::success ||
      world.configure_camera(entity, camera) != gneiss::result::success ||
      world.set_active_camera(entity) != gneiss::result::success) {
    return 2;
  }
  if (inspector.refresh(world.get(), entity) != gneiss::result::success) {
    return 20;
  }
  if (inspector.components().size() != 2U) {
    return 21;
  }
  if (inspector.components()[0].properties.size() != 3U ||
      inspector.components()[1].properties.size() != 4U) {
    return 22;
  }

  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  value.kind = GNEISS_PROPERTY_KIND_VEC3;
  value.payload.vec3_value = {.x = 2.0F, .y = 3.0F, .z = 4.0F};
  if (inspector.set_value(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, value) !=
      gneiss::result::success) {
    return 3;
  }
  gneiss::transform transform = GNEISS_TRANSFORM_IDENTITY;
  if (world.get_local_transform(entity, transform) != gneiss::result::success ||
      !nearly_equal(transform.translation[0], 2.0F)) {
    return 4;
  }

  value.payload.vec3_value = {.x = 0.0F, .y = 1.0F, .z = 1.0F};
  if (inspector.set_value(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_SCALE, value) !=
          gneiss::result::invalid_argument ||
      world.get_local_transform(entity, transform) != gneiss::result::success ||
      !nearly_equal(transform.scale[0], 1.0F) ||
      !nearly_equal(inspector.components()[0].properties[2].value.payload.vec3_value.x, 1.0F)) {
    return 5;
  }

  value.kind = GNEISS_PROPERTY_KIND_BOOL;
  value.payload.bool_value = 1U;
  if (inspector.set_value(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_IS_PRIMARY, value) !=
      gneiss::result::unsupported) {
    return 6;
  }
  if (inspector.refresh(world.get(), transform_only) != gneiss::result::success ||
      inspector.components().size() != 1U) {
    return 7;
  }
  if (world.destroy_entity(transform_only) != gneiss::result::success ||
      inspector.refresh(world.get(), transform_only) != gneiss::result::invalid_handle ||
      !inspector.components().empty()) {
    return 8;
  }
  return 0;
}
