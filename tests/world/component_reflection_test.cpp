// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

#include <cmath>

namespace {

[[nodiscard]] bool nearly_equal(float left, float right) noexcept {
  return std::abs(left - right) < 0.0001F;
}

} // namespace

int main() {
  gneiss::type_registry registry;
  gneiss::world first;
  gneiss::world second;
  gneiss::entity_id entity;
  gneiss::entity_id missing_component;
  gneiss::scene_node_id node;
  if (gneiss::type_registry::create(registry) != gneiss::result::success ||
      gneiss::world::register_reflection(registry) != gneiss::result::success ||
      gneiss::world::register_reflection(registry) != gneiss::result::success ||
      registry.freeze() != gneiss::result::success ||
      gneiss::world::create(first) != gneiss::result::success ||
      gneiss::world::create(second) != gneiss::result::success ||
      first.create_entity(entity) != gneiss::result::success ||
      first.create_entity(missing_component) != gneiss::result::success ||
      first.create_scene_node({}, entity, node) != gneiss::result::success) {
    return 1;
  }

  gneiss_type_info type_info{};
  if (registry.find_type(gneiss_transform_type_id(), type_info) != gneiss::result::success ||
      type_info.field_count != 3U ||
      registry.find_type(gneiss_camera_type_id(), type_info) != gneiss::result::success ||
      type_info.field_count != 4U) {
    return 2;
  }

  const gneiss_property_target target{.context = first.get(), .object = entity.get()};
  const gneiss_property_target missing_target{.context = first.get(),
                                              .object = missing_component.get()};
  const gneiss_property_target cross_world_target{.context = second.get(), .object = entity.get()};
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  if (registry.get_property(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, target,
                            value) != gneiss::result::success ||
      value.kind != GNEISS_PROPERTY_KIND_VEC3 || !nearly_equal(value.payload.vec3_value.x, 0.0F)) {
    return 3;
  }
  value.kind = GNEISS_PROPERTY_KIND_VEC3;
  value.payload.vec3_value = {.x = 2.0F, .y = 3.0F, .z = 4.0F};
  if (registry.set_property(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, target,
                            value) != gneiss::result::success) {
    return 4;
  }
  gneiss::transform transform = GNEISS_TRANSFORM_IDENTITY;
  if (first.get_local_transform(entity, transform) != gneiss::result::success ||
      !nearly_equal(transform.translation[0], 2.0F) ||
      registry.get_property(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION,
                            missing_target, value) != gneiss::result::not_found ||
      registry.get_property(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION,
                            cross_world_target, value) != gneiss::result::invalid_handle) {
    return 5;
  }

  value.kind = GNEISS_PROPERTY_KIND_VEC3;
  value.payload.vec3_value = {.x = 0.0F, .y = 1.0F, .z = 1.0F};
  if (registry.set_property(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_SCALE, target,
                            value) != gneiss::result::invalid_argument ||
      first.get_local_transform(entity, transform) != gneiss::result::success ||
      !nearly_equal(transform.scale[0], 1.0F)) {
    return 6;
  }

  gneiss::camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  if (first.configure_camera(entity, camera) != gneiss::result::success ||
      first.set_active_camera(entity) != gneiss::result::success ||
      registry.get_property(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_IS_PRIMARY, target,
                            value) != gneiss::result::success ||
      value.kind != GNEISS_PROPERTY_KIND_BOOL || value.payload.bool_value != 1U) {
    return 7;
  }
  value.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  value.payload.float32_value = 0.5F;
  if (registry.set_property(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target,
                            value) != gneiss::result::success ||
      first.get_camera(entity, camera) != gneiss::result::success ||
      !nearly_equal(camera.near_plane, 0.5F)) {
    return 8;
  }
  value.payload.float32_value = camera.far_plane + 1.0F;
  if (registry.set_property(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target,
                            value) != gneiss::result::invalid_argument ||
      first.get_camera(entity, camera) != gneiss::result::success ||
      !nearly_equal(camera.near_plane, 0.5F) ||
      registry.get_property(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, missing_target,
                            value) != gneiss::result::not_found) {
    return 9;
  }

  if (first.destroy_entity(entity) != gneiss::result::success ||
      registry.get_property(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target,
                            value) != gneiss::result::invalid_handle) {
    return 10;
  }
  return 0;
}
