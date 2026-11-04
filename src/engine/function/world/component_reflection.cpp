// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include <cmath>
#include <numbers>

namespace {
using namespace gneiss::engine;
template <auto Member> auto get_transform(const math::transform& object) { return object.*Member; }
template <auto Member, class Property>
error set_transform(math::transform& object, const Property& value) {
  auto updated = object;
  updated.*Member = value;
  float norm = 0;
  for (const auto item : math::components(updated.rotation))
    norm += item * item;
  if (!std::isfinite(norm) || std::abs(std::sqrt(norm) - 1.0F) > 1.0e-4F ||
      !std::ranges::all_of(math::components(updated.translation),
                           [](float item) { return std::isfinite(item); }) ||
      !std::ranges::all_of(math::components(updated.scale), [](float item) {
        return std::isfinite(item) && std::abs(item) >= 1.0e-6F;
      }))
    return error::invalid_argument;
  object = updated;
  return error::success;
}
template <auto Member> float get_camera(const camera_properties& object) {
  return object.settings.*Member;
}
template <auto Member, bool IsFov = false>
error set_camera(camera_properties& object, const float& scalar) {
  if (!std::isfinite(scalar) || scalar <= 0 || (IsFov && scalar >= std::numbers::pi_v<float>))
    return error::invalid_argument;
  // near/far 联合约束仍在 World 提交或全部 Prefab 覆盖完成后验证。
  object.settings.*Member = scalar;
  return error::success;
}
}

namespace {
gneiss::engine::error validate_transform_value(const gneiss::engine::math::transform& value) {
  return gneiss::engine::math::is_valid_transform(value) ? gneiss::engine::error::success
                                                         : gneiss::engine::error::invalid_argument;
}
}
gneiss::engine::error gneiss::engine::register_reflection() {
  auto operation =
      gneiss::engine::meta::type_builder<math::transform>{}
          .property<&get_transform<&math::transform::translation>,
                    &set_transform<&math::transform::translation, math::vector3>>(
              transform_translation_field, "translation", meta::property_flags::required)
          .property<&get_transform<&math::transform::rotation>,
                    &set_transform<&math::transform::rotation, math::quaternion>>(
              transform_rotation_field, "rotation", meta::property_flags::required)
          .property<&get_transform<&math::transform::scale>,
                    &set_transform<&math::transform::scale, math::vector3>>(
              transform_scale_field, "scale", meta::property_flags::required)
          .serialization()
          .validate<&validate_transform_value>()
          .commit();
  if (operation.failed())
    return operation;
  return gneiss::engine::meta::type_builder<camera_properties>{}
      .property<&get_camera<&math::camera_settings::fov>,
                &set_camera<&math::camera_settings::fov, true>>(camera_vertical_field_of_view_field,
                                                                "vertical_field_of_view_radians")
      .property<&get_camera<&math::camera_settings::near>,
                &set_camera<&math::camera_settings::near>>(camera_near_plane_field, "near_plane")
      .property<&get_camera<&math::camera_settings::far>, &set_camera<&math::camera_settings::far>>(
          camera_far_plane_field, "far_plane")
      .readonly<&camera_properties::is_primary>(camera_is_primary_field, "is_primary")
      .commit();
}

namespace gneiss::engine {
guid transform_property_type() noexcept { return transform_component_type; }
guid camera_property_type() noexcept { return camera_component_type; }
} // namespace gneiss::engine
