// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/math/transform.hpp"
#include "engine/core/reflection/component_identity.hpp"
#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/function/world/world_service.hpp"

#include "engine/core/rid.hpp"

#include "engine/core/rid.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace {

constexpr auto transform_id = gneiss::engine::transform_component_type;
constexpr auto camera_id = gneiss::engine::camera_component_type;
constexpr gneiss::engine::type_guid vec3_id{
    gneiss::engine::guid::from_bytes({0x90, 0xbc, 0xc8, 0x13, 0xa5, 0x3c, 0x41, 0x96, 0xaa, 0x35,
                                      0x93, 0xf8, 0x94, 0x6e, 0x82, 0xcb})};
constexpr gneiss::engine::type_guid quaternion_id{
    gneiss::engine::guid::from_bytes({0x21, 0xc1, 0x11, 0xbd, 0xd7, 0x54, 0x44, 0xf0, 0x89, 0xc1,
                                      0xf7, 0xcc, 0x27, 0x75, 0xd9, 0x1e})};
constexpr gneiss::engine::type_guid float32_id{
    gneiss::engine::guid::from_bytes({0xa2, 0x7e, 0x99, 0x02, 0xf3, 0xcb, 0x48, 0x45, 0xa7, 0xe8,
                                      0x32, 0x17, 0xf9, 0x75, 0x9f, 0xc5})};
constexpr gneiss::engine::type_guid bool_id{
    gneiss::engine::guid::from_bytes({0xc2, 0xaf, 0x2a, 0xd1, 0xa6, 0x25, 0x48, 0x42, 0xb4, 0xdd,
                                      0x74, 0xe9, 0x77, 0x06, 0x7e, 0x35})};

enum class transform_field : std::uint8_t { translation = 1, rotation = 2, scale = 3 };
enum class camera_field : std::uint8_t { field_of_view = 1, near_plane = 2, far_plane = 3 };

[[nodiscard]] gneiss::engine::rid target_world(gneiss::property_target target) noexcept {
  return static_cast<gneiss::engine::rid>(target.context);
}

[[nodiscard]] std::uint64_t target_entity(gneiss::property_target target) noexcept {
  return static_cast<std::uint64_t>(target.object);
}

gneiss::engine::result get_transform(void* user_data, gneiss::property_target target,
                                     gneiss::property_value* output) {
  gneiss::engine::transform transform;
  const auto result = gneiss::engine::entity_get_local_transform(target_world(target),
                                                                 target_entity(target), transform);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  switch (*static_cast<const transform_field*>(user_data)) {
  case transform_field::translation:
    output->payload = transform.translation;
    break;
  case transform_field::rotation:
    output->payload = transform.rotation;
    break;
  case transform_field::scale:
    output->payload = transform.scale;
    break;
  }
  return gneiss::engine::result::success;
}

gneiss::engine::result set_candidate_transform(void* user_data, void* candidate,
                                               const gneiss::property_value& input) {
  auto transform = *static_cast<gneiss::engine::transform*>(candidate);
  const auto* value = &input;
  switch (*static_cast<const transform_field*>(user_data)) {
  case transform_field::translation:
    transform.translation[0] = std::get<gneiss::property_vec3>(value->payload).x;
    transform.translation[1] = std::get<gneiss::property_vec3>(value->payload).y;
    transform.translation[2] = std::get<gneiss::property_vec3>(value->payload).z;
    break;
  case transform_field::rotation:
    transform.rotation[0] = std::get<gneiss::property_quaternion>(value->payload).x;
    transform.rotation[1] = std::get<gneiss::property_quaternion>(value->payload).y;
    transform.rotation[2] = std::get<gneiss::property_quaternion>(value->payload).z;
    transform.rotation[3] = std::get<gneiss::property_quaternion>(value->payload).w;
    break;
  case transform_field::scale:
    transform.scale[0] = std::get<gneiss::property_vec3>(value->payload).x;
    transform.scale[1] = std::get<gneiss::property_vec3>(value->payload).y;
    transform.scale[2] = std::get<gneiss::property_vec3>(value->payload).z;
    break;
  }
  float norm = 0;
  for (const auto item : gneiss::engine::components(transform.rotation)) {
    norm += item * item;
  }
  if (!std::isfinite(norm) || std::abs(std::sqrt(norm) - 1.0F) > 1.0e-4F ||
      !std::ranges::all_of(gneiss::engine::components(transform.translation),
                           [](float item) { return std::isfinite(item); }) ||
      !std::ranges::all_of(gneiss::engine::components(transform.scale), [](float item) {
        return std::isfinite(item) && std::abs(item) >= 1.0e-6F;
      })) {
    return gneiss::engine::result::invalid_argument;
  }
  *static_cast<gneiss::engine::transform*>(candidate) = transform;
  return gneiss::engine::result::success;
}

gneiss::engine::result set_transform(void* user_data, gneiss::property_target target,
                                     const gneiss::property_value* value) {
  gneiss::engine::transform transform;
  auto result = gneiss::engine::entity_get_local_transform(target_world(target),
                                                           target_entity(target), transform);
  if (result == gneiss::engine::result::success) {
    result = set_candidate_transform(user_data, &transform, *value);
  }
  return result == gneiss::engine::result::success
             ? gneiss::engine::entity_set_local_transform(target_world(target),
                                                          target_entity(target), transform)
             : result;
}

gneiss::engine::result get_camera(void* user_data, gneiss::property_target target,
                                  gneiss::property_value* output) {
  gneiss::engine::camera_settings camera;
  const auto result =
      gneiss::engine::entity_get_camera(target_world(target), target_entity(target), camera);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  switch (*static_cast<const camera_field*>(user_data)) {
  case camera_field::field_of_view:
    output->payload = camera.vertical_field_of_view_radians;
    break;
  case camera_field::near_plane:
    output->payload = camera.near_plane;
    break;
  case camera_field::far_plane:
    output->payload = camera.far_plane;
    break;
  }
  return gneiss::engine::result::success;
}

gneiss::engine::result set_candidate_camera(void* user_data, void* candidate,
                                            const gneiss::property_value& input) {
  auto camera = *static_cast<gneiss::engine::camera_settings*>(candidate);
  const auto* value = &input;
  const auto scalar = std::get<float>(value->payload);
  if (!std::isfinite(scalar) || scalar <= 0 ||
      (*static_cast<const camera_field*>(user_data) == camera_field::field_of_view &&
       scalar >= std::numbers::pi_v<float>)) {
    return gneiss::engine::result::invalid_argument;
  }
  switch (*static_cast<const camera_field*>(user_data)) {
  case camera_field::field_of_view:
    camera.vertical_field_of_view_radians = std::get<float>(value->payload);
    break;
  case camera_field::near_plane:
    camera.near_plane = std::get<float>(value->payload);
    break;
  case camera_field::far_plane:
    camera.far_plane = std::get<float>(value->payload);
    break;
  }
  // near/far 的联合约束在全部覆盖应用后由候选对象构造校验，避免依赖字段写入顺序。
  *static_cast<gneiss::engine::camera_settings*>(candidate) = camera;
  return gneiss::engine::result::success;
}

gneiss::engine::result set_camera(void* user_data, gneiss::property_target target,
                                  const gneiss::property_value* value) {
  gneiss::engine::camera_settings camera;
  auto result =
      gneiss::engine::entity_get_camera(target_world(target), target_entity(target), camera);
  if (result == gneiss::engine::result::success) {
    result = set_candidate_camera(user_data, &camera, *value);
  }
  return result == gneiss::engine::result::success
             ? gneiss::engine::entity_configure_camera(target_world(target), target_entity(target),
                                                       camera)
             : result;
}

gneiss::engine::result get_is_primary(void* /*user_data*/, gneiss::property_target target,
                                      gneiss::property_value* output) {
  gneiss::engine::camera_settings camera;
  auto result =
      gneiss::engine::entity_get_camera(target_world(target), target_entity(target), camera);
  if (result != gneiss::engine::result::success) {
    return result;
  }
  std::uint64_t active = UINT64_C(0);
  result = gneiss::engine::get_active_camera(target_world(target), active);
  if (result != gneiss::engine::result::success && result != gneiss::engine::result::not_ready) {
    return result;
  }
  output->payload = active == target_entity(target);
  return gneiss::engine::result::success;
}

constexpr transform_field translation_field = transform_field::translation;
constexpr transform_field rotation_field = transform_field::rotation;
constexpr transform_field scale_field = transform_field::scale;
constexpr camera_field field_of_view_field = camera_field::field_of_view;
constexpr camera_field near_plane_field = camera_field::near_plane;
constexpr camera_field far_plane_field = camera_field::far_plane;

[[nodiscard]] constexpr gneiss::engine::field_definition
make_field(std::uint32_t id, gneiss::engine::type_guid type_guid, std::uint32_t flags,
           const char* name, std::uint32_t name_length) noexcept {
  return {.id = gneiss::field_id{id},
          .type_guid = type_guid,
          .flags = static_cast<gneiss::field_flags>(flags),
          .name = {name, name_length}};
}

// Field ID 与属性类别均为定宽整数，调用点使用命名常量明确区分。
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
[[nodiscard]] gneiss::engine::result bind_component_field(
    const gneiss::engine::registry_ref& registry, gneiss::engine::type_guid type_id,
    std::uint32_t field_id, gneiss::property_kind kind,
    gneiss::engine::result (*getter)(void*, gneiss::property_target, gneiss::property_value*),
    gneiss::engine::result (*setter)(void*, gneiss::property_target, const gneiss::property_value*),
    const void* field) {
  const gneiss::engine::property_binding accessor{
      .kind = kind,
      .getter = getter,
      .setter = setter,

      .user_data = const_cast<void*>(field),
      .candidate_setter = setter == set_transform ? set_candidate_transform
                          : setter == set_camera  ? set_candidate_camera
                                                  : nullptr,
  };
  return gneiss::engine::bind_property(registry, type_id, gneiss::field_id{field_id}, accessor);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace

gneiss::engine::result
gneiss::engine::register_reflection(const gneiss::engine::registry_ref& registry) {
  constexpr std::array transform_fields{
      make_field(gneiss::engine::transform_translation_field.get(), vec3_id, 0U, "translation",
                 11U),
      make_field(gneiss::engine::transform_rotation_field.get(), quaternion_id, 0U, "rotation", 8U),
      make_field(gneiss::engine::transform_scale_field.get(), vec3_id, 0U, "scale", 5U)};
  constexpr std::array camera_fields{
      make_field(gneiss::engine::camera_vertical_field_of_view_field.get(), float32_id, 0U,
                 "vertical_field_of_view_radians", 30U),
      make_field(gneiss::engine::camera_near_plane_field.get(), float32_id, 0U, "near_plane", 10U),
      make_field(gneiss::engine::camera_far_plane_field.get(), float32_id, 0U, "far_plane", 9U),
      make_field(gneiss::engine::camera_is_primary_field.get(), bool_id,
                 static_cast<std::uint32_t>(gneiss::field_flags::read_only), "is_primary", 10U)};
  const gneiss::engine::type_definition transform{
      .guid = transform_id, .schema_version = 1U, .name = "transform", .fields = transform_fields};
  const gneiss::engine::type_definition camera{
      .guid = camera_id, .schema_version = 1U, .name = "camera", .fields = camera_fields};
  auto result = gneiss::engine::register_type<gneiss::engine::transform>(registry, transform);
  if (result == gneiss::engine::result::success) {
    result = gneiss::engine::register_type<gneiss::engine::camera_settings>(registry, camera);
  }
  if (result == gneiss::engine::result::success) {
    result = bind_component_field(
        registry, transform_id, gneiss::engine::transform_translation_field.get(),
        gneiss::property_kind::vec3, get_transform, set_transform, &translation_field);
  }
  if (result == gneiss::engine::result::success) {
    result = bind_component_field(
        registry, transform_id, gneiss::engine::transform_rotation_field.get(),
        gneiss::property_kind::quaternion, get_transform, set_transform, &rotation_field);
  }
  if (result == gneiss::engine::result::success) {
    result = bind_component_field(
        registry, transform_id, gneiss::engine::transform_scale_field.get(),
        gneiss::property_kind::vec3, get_transform, set_transform, &scale_field);
  }
  if (result == gneiss::engine::result::success) {
    result = bind_component_field(
        registry, camera_id, gneiss::engine::camera_vertical_field_of_view_field.get(),
        gneiss::property_kind::float32, get_camera, set_camera, &field_of_view_field);
  }
  if (result == gneiss::engine::result::success) {
    result = bind_component_field(
        registry, camera_id, gneiss::engine::camera_near_plane_field.get(),
        gneiss::property_kind::float32, get_camera, set_camera, &near_plane_field);
  }
  if (result == gneiss::engine::result::success) {
    result = bind_component_field(registry, camera_id, gneiss::engine::camera_far_plane_field.get(),
                                  gneiss::property_kind::float32, get_camera, set_camera,
                                  &far_plane_field);
  }
  if (result == gneiss::engine::result::success) {
    result =
        bind_component_field(registry, camera_id, gneiss::engine::camera_is_primary_field.get(),
                             gneiss::property_kind::boolean, get_is_primary, nullptr, nullptr);
  }
  return result;
}

namespace gneiss::engine {
gneiss::engine::result create_property_registry(gneiss::engine::registry_ref& output) noexcept {
  gneiss::engine::registry_ref pending;
  auto operation = gneiss::engine::create_native(pending);
  if (operation == gneiss::engine::result::success) {
    operation = register_reflection(pending);
  }
  if (operation == gneiss::engine::result::success) {
    operation = gneiss::engine::freeze(pending);
  }
  if (operation == gneiss::engine::result::success) {
    output = std::move(pending);
  }
  return operation;
}
gneiss::engine::type_guid transform_property_type() noexcept { return transform_component_type; }
gneiss::engine::type_guid camera_property_type() noexcept { return camera_component_type; }
} // namespace gneiss::engine
