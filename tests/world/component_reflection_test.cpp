// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/c_owner.hpp"
#include <gneiss/engine/world.hpp>

#include <cmath>

using namespace gneiss;

namespace {

[[nodiscard]] bool nearly_equal(float left, float right) noexcept {
  return std::abs(left - right) < 0.0001F;
}

} // namespace

int main() {
  gneiss::test_support::registry_owner registry;
  gneiss::test_support::world_owner first;
  gneiss::test_support::world_owner second;
  gneiss::entity_id entity;
  gneiss::entity_id missing_component;
  gneiss::scene_node_id node;
  if (gneiss::result{gneiss_type_registry_create(registry.put())} != gneiss::result::success ||
      gneiss::result{gneiss_world_register_reflection(registry.get())} != gneiss::result::success ||
      gneiss::result{gneiss_world_register_reflection(registry.get())} != gneiss::result::success ||
      ([&]() { return from_native(gneiss_type_registry_freeze(registry.get())); })() !=
          gneiss::result::success ||
      ([&] {
        const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
        return gneiss::result{gneiss_world_create(&desc, first.put())};
      })() != gneiss::result::success ||
      ([&] {
        const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
        return gneiss::result{gneiss_world_create(&desc, second.put())};
      })() != gneiss::result::success ||
      ([&](entity_id& out_entity) {
        gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
        const auto native_result = gneiss_world_entity_create(first.get(), &native_entity);
        if (native_result == GNEISS_SUCCESS) {
          out_entity = entity_id{native_entity};
        }
        return from_native(native_result);
      })(entity) != gneiss::result::success ||
      ([&](entity_id& out_entity) {
        gneiss_entity_id native_entity = GNEISS_NULL_ENTITY_ID;
        const auto native_result = gneiss_world_entity_create(first.get(), &native_entity);
        if (native_result == GNEISS_SUCCESS) {
          out_entity = entity_id{native_entity};
        }
        return from_native(native_result);
      })(missing_component) != gneiss::result::success ||
      ([&](scene_node_id parent, entity_id entity, scene_node_id& out_node) {
        gneiss_scene_node_id native_node = GNEISS_NULL_SCENE_NODE_ID;
        const auto native_result =
            gneiss_scene_node_create(first.get(), parent.get(), entity.get(), &native_node);
        if (native_result == GNEISS_SUCCESS) {
          out_node = scene_node_id{native_node};
        }
        return from_native(native_result);
      })({}, entity, node) != gneiss::result::success) {
    return 1;
  }

  gneiss_type_info type_info{};
  if (([&](gneiss_type_id id, gneiss_type_info& output) {
        output = GNEISS_TYPE_INFO_INIT;
        return from_native(gneiss_type_registry_find_type(registry.get(), id, &output));
      })(gneiss_transform_type_id(), type_info) != gneiss::result::success ||
      type_info.field_count != 3U ||
      ([&](gneiss_type_id id, gneiss_type_info& output) {
        output = GNEISS_TYPE_INFO_INIT;
        return from_native(gneiss_type_registry_find_type(registry.get(), id, &output));
      })(gneiss_camera_type_id(), type_info) != gneiss::result::success ||
      type_info.field_count != 4U) {
    return 2;
  }

  const gneiss_property_target target{.context = first.get(), .object = entity.get()};
  const gneiss_property_target missing_target{.context = first.get(),
                                              .object = missing_component.get()};
  const gneiss_property_target cross_world_target{.context = second.get(), .object = entity.get()};
  gneiss_property_value value = GNEISS_PROPERTY_VALUE_INIT;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, target, value) !=
          gneiss::result::success ||
      value.kind != GNEISS_PROPERTY_KIND_VEC3 || !nearly_equal(value.payload.vec3_value.x, 0.0F)) {
    return 3;
  }
  value.kind = GNEISS_PROPERTY_KIND_VEC3;
  value.payload.vec3_value = {.x = 2.0F, .y = 3.0F, .z = 4.0F};
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, target, value) !=
      gneiss::result::success) {
    return 4;
  }
  gneiss::transform transform{};
  if (([&](entity_id entity, gneiss::transform& output) {
        gneiss_transform native{};
        const auto status = from_native(
            gneiss_world_entity_get_local_transform(first.get(), entity.get(), &native));
        if (status.ok()) {
          output = from_native(native);
        }
        return status;
      })(entity, transform) != gneiss::result::success ||
      !nearly_equal(transform.translation[0], 2.0F) ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, missing_target, value) !=
          gneiss::result::not_found ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION, cross_world_target,
         value) != gneiss::result::invalid_handle) {
    return 5;
  }

  value.kind = GNEISS_PROPERTY_KIND_VEC3;
  value.payload.vec3_value = {.x = 0.0F, .y = 1.0F, .z = 1.0F};
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(gneiss_transform_type_id(), GNEISS_TRANSFORM_FIELD_SCALE, target, value) !=
          gneiss::result::invalid_argument ||
      ([&](entity_id entity, gneiss::transform& output) {
        gneiss_transform native{};
        const auto status = from_native(
            gneiss_world_entity_get_local_transform(first.get(), entity.get(), &native));
        if (status.ok()) {
          output = from_native(native);
        }
        return status;
      })(entity, transform) != gneiss::result::success ||
      !nearly_equal(transform.scale[0], 1.0F)) {
    return 6;
  }

  gneiss::camera_desc camera{};
  if (([&](entity_id entity, const camera_desc& value) {
        const auto native = to_native(value);
        return from_native(
            gneiss_world_entity_configure_camera(first.get(), entity.get(), &native));
      })(entity, camera) != gneiss::result::success ||
      ([&](entity_id entity) {
        return from_native(gneiss_world_set_active_camera(first.get(), entity.get()));
      })(entity) != gneiss::result::success ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_IS_PRIMARY, target, value) !=
          gneiss::result::success ||
      value.kind != GNEISS_PROPERTY_KIND_BOOL || value.payload.bool_value != 1U) {
    return 7;
  }
  value.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  value.payload.float32_value = 0.5F;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target, value) !=
          gneiss::result::success ||
      ([&](entity_id entity, camera_desc& out_camera) {
        gneiss_camera_desc native = GNEISS_CAMERA_DESC_INIT;
        const auto status =
            from_native(gneiss_world_entity_get_camera(first.get(), entity.get(), &native));
        if (status.ok()) {
          out_camera = from_native(native);
        }
        return status;
      })(entity, camera) != gneiss::result::success ||
      !nearly_equal(camera.near_plane, 0.5F)) {
    return 8;
  }
  value.payload.float32_value = camera.far_plane + 1.0F;
  if (([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           const gneiss_property_value& value) {
        return from_native(
            gneiss_type_registry_set_property(registry.get(), type_id, field_id, target, &value));
      })(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target, value) !=
          gneiss::result::invalid_argument ||
      ([&](entity_id entity, camera_desc& out_camera) {
        gneiss_camera_desc native = GNEISS_CAMERA_DESC_INIT;
        const auto status =
            from_native(gneiss_world_entity_get_camera(first.get(), entity.get(), &native));
        if (status.ok()) {
          out_camera = from_native(native);
        }
        return status;
      })(entity, camera) != gneiss::result::success ||
      !nearly_equal(camera.near_plane, 0.5F) ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, missing_target, value) !=
          gneiss::result::not_found) {
    return 9;
  }

  if (([&](entity_id entity) {
        return from_native(gneiss_world_entity_destroy(first.get(), entity.get()));
      })(entity) != gneiss::result::success ||
      ([&](gneiss_type_id type_id, gneiss_field_id field_id, gneiss_property_target target,
           gneiss_property_value& output) {
        return from_native(
            gneiss_type_registry_get_property(registry.get(), type_id, field_id, target, &output));
      })(gneiss_camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, target, value) !=
          gneiss::result::invalid_handle) {
    return 10;
  }
  return 0;
}
