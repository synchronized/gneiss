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
result set_transform(math::transform& object, const Property& value) {
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
    return result::invalid_argument;
  object = updated;
  return result::success;
}
template <auto Member> float get_camera(const camera_properties& object) {
  return object.settings.*Member;
}
template <auto Member, bool IsFov = false>
result set_camera(camera_properties& object, const float& scalar) {
  if (!std::isfinite(scalar) || scalar <= 0 || (IsFov && scalar >= std::numbers::pi_v<float>))
    return result::invalid_argument;
  // near/far 联合约束仍在 World 提交或全部 Prefab 覆盖完成后验证。
  object.settings.*Member = scalar;
  return result::success;
}
}

gneiss::engine::result
gneiss::engine::register_reflection(const gneiss::engine::meta::registry_ref& registry) {
  auto operation = gneiss::engine::meta::type_builder<math::transform>{registry}
                       .property<&get_transform<&math::transform::translation>,
                                 &set_transform<&math::transform::translation, math::vector3>>(
                           transform_translation_field, "translation")
                       .property<&get_transform<&math::transform::rotation>,
                                 &set_transform<&math::transform::rotation, math::quaternion>>(
                           transform_rotation_field, "rotation")
                       .property<&get_transform<&math::transform::scale>,
                                 &set_transform<&math::transform::scale, math::vector3>>(
                           transform_scale_field, "scale")
                       .commit();
  if (operation.failed())
    return operation;
  return gneiss::engine::meta::type_builder<camera_properties>{registry}
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
gneiss::engine::result
create_property_registry(gneiss::engine::meta::registry_ref& output) noexcept {
  gneiss::engine::meta::registry_ref pending;
  auto operation = gneiss::engine::meta::create_registry(pending);
  if (operation == gneiss::engine::result::success) {
    operation = register_reflection(pending);
  }
  if (operation == gneiss::engine::result::success) {
    operation = gneiss::engine::meta::freeze(pending);
  }
  if (operation == gneiss::engine::result::success) {
    output = std::move(pending);
  }
  return operation;
}
gneiss::engine::guid transform_property_type() noexcept { return transform_component_type; }
gneiss::engine::guid camera_property_type() noexcept { return camera_component_type; }
} // namespace gneiss::engine

namespace gneiss::engine {
namespace {
/** World 接口负责线程与实体校验；反射只修改副本，成功后才提交。 */
template <class Access>
result access_component(world& world, std::uint64_t entity, guid type, bool write,
                        Access&& access) noexcept {
  if (type == transform_component_type) {
    math::transform value;
    auto operation = world.entity_get_local_transform(entity_id{entity}, value);
    if (operation.failed())
      return operation;
    operation = access(&value);
    return operation.failed() || !write
               ? operation
               : world.entity_set_local_transform(entity_id{entity}, value);
  }
  if (type == camera_component_type) {
    camera_properties value;
    auto operation = world.entity_get_camera(entity_id{entity}, value.settings);
    if (operation.failed())
      return operation;
    entity_id active{};
    operation = world.get_active_camera(active);
    if (operation.failed() && operation != result::not_ready)
      return operation;
    value.is_primary = active.get() == entity;
    operation = access(&value);
    return operation.failed() || !write
               ? operation
               : world.entity_configure_camera(entity_id{entity}, value.settings);
  }
  return result::unsupported;
}
}
result read_component_property(const gneiss::engine::meta::registry_ref& registry, world& world,
                               std::uint64_t entity, guid type,
                               gneiss::engine::meta::property_id field,
                               gneiss::engine::value& value) noexcept {
  if (!registry)
    return result::invalid_handle;
  return access_component(world, entity, type, false, [&](auto* object) {
    return read_property(registry, type, field, object, value);
  });
}
result write_component_property(const gneiss::engine::meta::registry_ref& registry, world& world,
                                std::uint64_t entity, guid type,
                                gneiss::engine::meta::property_id field,
                                const gneiss::engine::value& value) noexcept {
  if (!registry)
    return result::invalid_handle;
  return access_component(world, entity, type, true, [&](auto* object) {
    return write_property(registry, type, field, object, value);
  });
}
}
