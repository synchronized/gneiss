// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/math/camera.hpp"
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/serialization/policy.hpp"
namespace gneiss::engine::serialization {
using namespace meta;
namespace {
template <auto Member> auto get_transform(const math::transform& object) { return object.*Member; }
template <auto Member, class Property>
error set_transform(math::transform& object, const Property& value) {
  auto updated = object;
  updated.*Member = value;
  if (!math::is_valid_transform(updated))
    return error::invalid_argument;
  object = updated;
  return error::success;
}

}
namespace detail {
// 仅由内置注册入口调用，不额外暴露数学注册的初始化步骤。
error register_math_types(const registry_ref& registry) noexcept {
  auto status = registration<math::transform>{registry}
                    .property<&get_transform<&math::transform::translation>,
                              &set_transform<&math::transform::translation, math::vector3>>(
                        property_id{1}, "translation")(field_policy{.required = true})
                    .property<&get_transform<&math::transform::rotation>,
                              &set_transform<&math::transform::rotation, math::quaternion>>(
                        property_id{2}, "rotation")(field_policy{.required = true})
                    .property<&get_transform<&math::transform::scale>,
                              &set_transform<&math::transform::scale, math::vector3>>(
                        property_id{3}, "scale")(field_policy{.required = true})
                    .commit();
  if (status.failed())
    return status;
  return registration<math::camera_settings>{registry}
      .property<&math::camera_settings::fov>(property_id{1}, "fov")(field_policy{.required = true})
      .property<&math::camera_settings::near>(property_id{2},
                                              "near")(field_policy{.required = true})
      .property<&math::camera_settings::far>(property_id{3}, "far")(field_policy{.required = true})
      .commit();
}
}
}
