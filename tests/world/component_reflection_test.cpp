// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/function/world/world_service.hpp"
#include <cmath>

int main() {
  namespace engine = gneiss::engine;
  using engine::result;
  engine::registry_ref registry;
  engine::world_owner first, second;
  engine::rid first_context{}, second_context{};
  gneiss::entity_id entity, missing;
  gneiss::scene_node_id node;
  if (engine::create_native(registry).failed() || engine::register_reflection(registry).failed() ||
      engine::register_reflection(registry).failed() || engine::freeze(registry).failed() ||
      engine::create_native(first, first_context).failed() ||
      engine::create_native(second, second_context).failed() ||
      first->entity_create(entity).failed() || first->entity_create(missing).failed() ||
      first->node_create({}, entity, node).failed())
    return 1;
  const auto transform_type = engine::transform_property_type();
  const auto camera_type = engine::camera_property_type();
  gneiss::type_info info;
  if (engine::describe_type(registry, transform_type, info).failed() || info.fields.size() != 3 ||
      engine::describe_type(registry, camera_type, info).failed() || info.fields.size() != 4)
    return 2;
  const gneiss::property_target target{first_context, entity.get()};
  const gneiss::property_target missing_target{first_context, missing.get()};
  const gneiss::property_target cross_world_target{second_context, entity.get()};
  gneiss::property_value value;
  if (engine::get_property(registry, transform_type, 1, target, value).failed() ||
      value.kind() != gneiss::property_kind::vec3 ||
      std::abs(std::get<gneiss::property_vec3>(value.payload).x) > 0.0001F)
    return 3;
  value.payload = gneiss::property_vec3{2, 3, 4};
  if (engine::set_property(registry, transform_type, 1, target, value).failed())
    return 4;
  engine::transform transform;
  if (first->entity_get_local_transform(entity, transform).failed() ||
      std::abs(transform.translation[0] - 2) > 0.0001F ||
      engine::get_property(registry, transform_type, 1, missing_target, value) !=
          result::not_found ||
      engine::get_property(registry, transform_type, 1, cross_world_target, value) !=
          result::invalid_handle)
    return 5;
  value.payload = gneiss::property_vec3{0, 1, 1};
  if (engine::set_property(registry, transform_type, 3, target, value) !=
          result::invalid_argument ||
      first->entity_get_local_transform(entity, transform).failed() ||
      std::abs(transform.scale[0] - 1) > 0.0001F)
    return 6;
  engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (first->entity_set_camera(entity, camera, true).failed() ||
      engine::get_property(registry, camera_type, 4, target, value).failed() ||
      value.kind() != gneiss::property_kind::boolean || !std::get<bool>(value.payload))
    return 7;
  value.payload = 0.5F;
  if (engine::set_property(registry, camera_type, 2, target, value).failed() ||
      first->entity_get_camera(entity, camera).failed() ||
      std::abs(camera.near_plane - 0.5F) > 0.0001F)
    return 8;
  value.payload = camera.far_plane + 1;
  if (engine::set_property(registry, camera_type, 2, target, value) != result::invalid_argument ||
      first->entity_get_camera(entity, camera).failed() ||
      std::abs(camera.near_plane - 0.5F) > 0.0001F ||
      engine::get_property(registry, camera_type, 2, missing_target, value) != result::not_found)
    return 9;
  if (first->entity_destroy(entity).failed() ||
      engine::get_property(registry, camera_type, 2, target, value) != result::invalid_handle)
    return 10;
  return 0;
}
