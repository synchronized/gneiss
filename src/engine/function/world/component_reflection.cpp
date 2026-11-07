// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/math/transform.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include <cmath>

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
}

namespace {
gneiss::engine::error validate_transform_value(const gneiss::engine::math::transform& value) {
  return gneiss::engine::math::is_valid_transform(value) ? gneiss::engine::error::success
                                                         : gneiss::engine::error::invalid_argument;
}
}
gneiss::engine::error gneiss::engine::register_reflection() {
  return gneiss::engine::meta::type_builder<math::transform>{}
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
}

namespace gneiss::engine {
guid transform_property_type() noexcept { return transform_component_type; }
} // namespace gneiss::engine
