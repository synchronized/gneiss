// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/transform.hpp"
#include "engine/core/reflection/type_registry.hpp"
#include "engine/function/world/world_service.hpp"

#include <gneiss/engine/world.h>

#include <gneiss/engine/render.h>
#include <gneiss/engine/scene.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace {

constexpr gneiss_type_id transform_id{{0x69, 0x64, 0x4f, 0x20, 0xb2, 0xd2, 0x4e, 0x48, 0x8c, 0x74,
                                       0x91, 0xf4, 0xf9, 0x52, 0xec, 0x2d}};
constexpr gneiss_type_id camera_id{{0xd1, 0x3a, 0x72, 0xf5, 0x71, 0xed, 0x43, 0xd6, 0x98, 0x98,
                                    0x12, 0x06, 0x3f, 0xd3, 0x71, 0x83}};
constexpr gneiss_type_id vec3_id{{0x90, 0xbc, 0xc8, 0x13, 0xa5, 0x3c, 0x41, 0x96, 0xaa, 0x35, 0x93,
                                  0xf8, 0x94, 0x6e, 0x82, 0xcb}};
constexpr gneiss_type_id quaternion_id{{0x21, 0xc1, 0x11, 0xbd, 0xd7, 0x54, 0x44, 0xf0, 0x89, 0xc1,
                                        0xf7, 0xcc, 0x27, 0x75, 0xd9, 0x1e}};
constexpr gneiss_type_id float32_id{{0xa2, 0x7e, 0x99, 0x02, 0xf3, 0xcb, 0x48, 0x45, 0xa7, 0xe8,
                                     0x32, 0x17, 0xf9, 0x75, 0x9f, 0xc5}};
constexpr gneiss_type_id bool_id{{0xc2, 0xaf, 0x2a, 0xd1, 0xa6, 0x25, 0x48, 0x42, 0xb4, 0xdd, 0x74,
                                  0xe9, 0x77, 0x06, 0x7e, 0x35}};

enum class transform_field : std::uint8_t { translation = 1, rotation = 2, scale = 3 };
enum class camera_field : std::uint8_t { field_of_view = 1, near_plane = 2, far_plane = 3 };

[[nodiscard]] gneiss_world target_world(gneiss_property_target target) noexcept {
  return static_cast<gneiss_world>(target.context);
}

[[nodiscard]] gneiss_entity_id target_entity(gneiss_property_target target) noexcept {
  return static_cast<gneiss_entity_id>(target.object);
}

gneiss_result get_transform(void* user_data, gneiss_property_target target,
                            gneiss_property_value* output) {
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  const auto result = gneiss::world_internal::entity_get_local_transform(
      target_world(target), target_entity(target), transform);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  switch (*static_cast<const transform_field*>(user_data)) {
  case transform_field::translation:
    output->kind = GNEISS_PROPERTY_KIND_VEC3;
    output->payload.vec3_value = {.x = transform.translation[0],
                                  .y = transform.translation[1],
                                  .z = transform.translation[2]};
    break;
  case transform_field::rotation:
    output->kind = GNEISS_PROPERTY_KIND_QUATERNION;
    output->payload.quaternion_value = {.x = transform.rotation[0],
                                        .y = transform.rotation[1],
                                        .z = transform.rotation[2],
                                        .w = transform.rotation[3]};
    break;
  case transform_field::scale:
    output->kind = GNEISS_PROPERTY_KIND_VEC3;
    output->payload.vec3_value = {
        .x = transform.scale[0], .y = transform.scale[1], .z = transform.scale[2]};
    break;
  }
  return GNEISS_SUCCESS;
}

gneiss_result set_candidate_transform(void* user_data, void* candidate,
                                      const gneiss_property_value& input) {
  auto transform = *static_cast<gneiss::engine::transform*>(candidate);
  const auto* value = &input;
  switch (*static_cast<const transform_field*>(user_data)) {
  case transform_field::translation:
    transform.translation[0] = value->payload.vec3_value.x;
    transform.translation[1] = value->payload.vec3_value.y;
    transform.translation[2] = value->payload.vec3_value.z;
    break;
  case transform_field::rotation:
    transform.rotation[0] = value->payload.quaternion_value.x;
    transform.rotation[1] = value->payload.quaternion_value.y;
    transform.rotation[2] = value->payload.quaternion_value.z;
    transform.rotation[3] = value->payload.quaternion_value.w;
    break;
  case transform_field::scale:
    transform.scale[0] = value->payload.vec3_value.x;
    transform.scale[1] = value->payload.vec3_value.y;
    transform.scale[2] = value->payload.vec3_value.z;
    break;
  }
  float norm = 0;
  for (const auto item : transform.rotation) {
    norm += item * item;
  }
  if (!std::isfinite(norm) || std::abs(std::sqrt(norm) - 1.0F) > 1.0e-4F ||
      !std::ranges::all_of(transform.translation, [](float item) { return std::isfinite(item); }) ||
      !std::ranges::all_of(transform.scale, [](float item) {
        return std::isfinite(item) && std::abs(item) >= 1.0e-6F;
      })) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  *static_cast<gneiss::engine::transform*>(candidate) = transform;
  return GNEISS_SUCCESS;
}

gneiss_result set_transform(void* user_data, gneiss_property_target target,
                            const gneiss_property_value* value) {
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  auto result = gneiss::world_internal::entity_get_local_transform(
      target_world(target), target_entity(target), transform);
  if (result == GNEISS_SUCCESS) {
    gneiss::engine::transform candidate;
    std::ranges::copy(transform.translation, candidate.translation.begin());
    std::ranges::copy(transform.rotation, candidate.rotation.begin());
    std::ranges::copy(transform.scale, candidate.scale.begin());
    result = set_candidate_transform(user_data, &candidate, *value);
    if (result == GNEISS_SUCCESS) {
      std::ranges::copy(candidate.translation, transform.translation);
      std::ranges::copy(candidate.rotation, transform.rotation);
      std::ranges::copy(candidate.scale, transform.scale);
    }
  }
  return result == GNEISS_SUCCESS ? gneiss::world_internal::entity_set_local_transform(
                                        target_world(target), target_entity(target), transform)
                                  : result;
}

gneiss_result get_camera(void* user_data, gneiss_property_target target,
                         gneiss_property_value* output) {
  gneiss::world_internal::camera_settings camera;
  const auto result = gneiss::world_internal::entity_get_camera(target_world(target),
                                                                target_entity(target), camera);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  output->kind = GNEISS_PROPERTY_KIND_FLOAT32;
  switch (*static_cast<const camera_field*>(user_data)) {
  case camera_field::field_of_view:
    output->payload.float32_value = camera.vertical_field_of_view_radians;
    break;
  case camera_field::near_plane:
    output->payload.float32_value = camera.near_plane;
    break;
  case camera_field::far_plane:
    output->payload.float32_value = camera.far_plane;
    break;
  }
  return GNEISS_SUCCESS;
}

gneiss_result set_candidate_camera(void* user_data, void* candidate,
                                   const gneiss_property_value& input) {
  auto camera = *static_cast<gneiss::world_internal::camera_settings*>(candidate);
  const auto* value = &input;
  const auto scalar = value->payload.float32_value;
  if (!std::isfinite(scalar) || scalar <= 0 ||
      (*static_cast<const camera_field*>(user_data) == camera_field::field_of_view &&
       scalar >= std::numbers::pi_v<float>)) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  switch (*static_cast<const camera_field*>(user_data)) {
  case camera_field::field_of_view:
    camera.vertical_field_of_view_radians = value->payload.float32_value;
    break;
  case camera_field::near_plane:
    camera.near_plane = value->payload.float32_value;
    break;
  case camera_field::far_plane:
    camera.far_plane = value->payload.float32_value;
    break;
  }
  // near/far 的联合约束在全部覆盖应用后由候选对象构造校验，避免依赖字段写入顺序。
  *static_cast<gneiss::world_internal::camera_settings*>(candidate) = camera;
  return GNEISS_SUCCESS;
}

gneiss_result set_camera(void* user_data, gneiss_property_target target,
                         const gneiss_property_value* value) {
  gneiss::world_internal::camera_settings camera;
  auto result = gneiss::world_internal::entity_get_camera(target_world(target),
                                                          target_entity(target), camera);
  if (result == GNEISS_SUCCESS) {
    result = set_candidate_camera(user_data, &camera, *value);
  }
  return result == GNEISS_SUCCESS ? gneiss::world_internal::entity_configure_camera(
                                        target_world(target), target_entity(target), camera)
                                  : result;
}

gneiss_result get_is_primary(void* /*user_data*/, gneiss_property_target target,
                             gneiss_property_value* output) {
  gneiss::world_internal::camera_settings camera;
  auto result = gneiss::world_internal::entity_get_camera(target_world(target),
                                                          target_entity(target), camera);
  if (result != GNEISS_SUCCESS) {
    return result;
  }
  gneiss_entity_id active = GNEISS_NULL_ENTITY_ID;
  result = gneiss::world_internal::get_active_camera(target_world(target), active);
  if (result != GNEISS_SUCCESS && result != GNEISS_ERROR_NOT_READY) {
    return result;
  }
  output->kind = GNEISS_PROPERTY_KIND_BOOL;
  output->payload.bool_value = active == target_entity(target) ? UINT8_C(1) : UINT8_C(0);
  return GNEISS_SUCCESS;
}

constexpr transform_field translation_field = transform_field::translation;
constexpr transform_field rotation_field = transform_field::rotation;
constexpr transform_field scale_field = transform_field::scale;
constexpr camera_field field_of_view_field = camera_field::field_of_view;
constexpr camera_field near_plane_field = camera_field::near_plane;
constexpr camera_field far_plane_field = camera_field::far_plane;

[[nodiscard]] constexpr gneiss::reflection_internal::field_definition
make_field(gneiss_field_id id, gneiss_type_id value_type_id, std::uint32_t flags, const char* name,
           std::uint32_t name_length) noexcept {
  return {.id = id, .value_type_id = value_type_id, .flags = flags, .name = {name, name_length}};
}

// Field ID 与属性类别均为定宽整数，调用点使用命名常量明确区分。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
[[nodiscard]] gneiss_result bind(gneiss_type_registry registry, gneiss_type_id type_id,
                                 gneiss_field_id field_id, std::uint32_t kind,
                                 gneiss_property_getter getter, gneiss_property_setter setter,
                                 const void* field) {
  const gneiss::reflection_internal::property_binding accessor{
      .kind = kind,
      .getter = getter,
      .setter = setter,
      .user_data = const_cast<void*>(field),
      .candidate_setter = setter == set_transform ? set_candidate_transform
                          : setter == set_camera  ? set_candidate_camera
                                                  : nullptr,
  };
  return gneiss::reflection_internal::bind_property(gneiss::reflection_internal::resolve(registry),
                                                    type_id, field_id, accessor);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace

gneiss_type_id gneiss::world_internal::transform_type_id() noexcept { return transform_id; }

gneiss_type_id gneiss::world_internal::camera_type_id() noexcept { return camera_id; }

gneiss_result gneiss::world_internal::register_reflection(gneiss_type_registry registry) {
  constexpr std::array transform_fields{
      make_field(GNEISS_TRANSFORM_FIELD_TRANSLATION, vec3_id, 0U, "translation", 11U),
      make_field(GNEISS_TRANSFORM_FIELD_ROTATION, quaternion_id, 0U, "rotation", 8U),
      make_field(GNEISS_TRANSFORM_FIELD_SCALE, vec3_id, 0U, "scale", 5U)};
  constexpr std::array camera_fields{
      make_field(GNEISS_CAMERA_FIELD_VERTICAL_FIELD_OF_VIEW_RADIANS, float32_id, 0U,
                 "vertical_field_of_view_radians", 30U),
      make_field(GNEISS_CAMERA_FIELD_NEAR_PLANE, float32_id, 0U, "near_plane", 10U),
      make_field(GNEISS_CAMERA_FIELD_FAR_PLANE, float32_id, 0U, "far_plane", 9U),
      make_field(GNEISS_CAMERA_FIELD_IS_PRIMARY, bool_id, GNEISS_FIELD_FLAG_READ_ONLY, "is_primary",
                 10U)};
  const gneiss::reflection_internal::type_definition transform{
      .id = transform_id, .schema_version = 1U, .name = "transform", .fields = transform_fields};
  const gneiss::reflection_internal::type_definition camera{
      .id = camera_id, .schema_version = 1U, .name = "camera", .fields = camera_fields};
  auto result = gneiss::reflection_internal::register_type(
      gneiss::reflection_internal::resolve(registry), transform);
  if (result == GNEISS_SUCCESS) {
    result = gneiss::reflection_internal::register_type(
        gneiss::reflection_internal::resolve(registry), camera);
  }
  if (result == GNEISS_SUCCESS) {
    result = bind(registry, transform_id, GNEISS_TRANSFORM_FIELD_TRANSLATION,
                  GNEISS_PROPERTY_KIND_VEC3, get_transform, set_transform, &translation_field);
  }
  if (result == GNEISS_SUCCESS) {
    result = bind(registry, transform_id, GNEISS_TRANSFORM_FIELD_ROTATION,
                  GNEISS_PROPERTY_KIND_QUATERNION, get_transform, set_transform, &rotation_field);
  }
  if (result == GNEISS_SUCCESS) {
    result = bind(registry, transform_id, GNEISS_TRANSFORM_FIELD_SCALE, GNEISS_PROPERTY_KIND_VEC3,
                  get_transform, set_transform, &scale_field);
  }
  if (result == GNEISS_SUCCESS) {
    result = bind(registry, camera_id, GNEISS_CAMERA_FIELD_VERTICAL_FIELD_OF_VIEW_RADIANS,
                  GNEISS_PROPERTY_KIND_FLOAT32, get_camera, set_camera, &field_of_view_field);
  }
  if (result == GNEISS_SUCCESS) {
    result = bind(registry, camera_id, GNEISS_CAMERA_FIELD_NEAR_PLANE, GNEISS_PROPERTY_KIND_FLOAT32,
                  get_camera, set_camera, &near_plane_field);
  }
  if (result == GNEISS_SUCCESS) {
    result = bind(registry, camera_id, GNEISS_CAMERA_FIELD_FAR_PLANE, GNEISS_PROPERTY_KIND_FLOAT32,
                  get_camera, set_camera, &far_plane_field);
  }
  if (result == GNEISS_SUCCESS) {
    result = bind(registry, camera_id, GNEISS_CAMERA_FIELD_IS_PRIMARY, GNEISS_PROPERTY_KIND_BOOL,
                  get_is_primary, nullptr, nullptr);
  }
  return result;
}
