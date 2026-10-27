// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/meta/type_registration.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include <cmath>
#include <thread>

int main() {
  namespace engine = gneiss::engine;
  using engine::result;
  gneiss::engine::meta::registry_ref registry;
  engine::world_owner first, second;
  gneiss::entity_id entity, missing;
  gneiss::scene_node_id node;
  if (gneiss::engine::meta::create_native(registry).failed() ||
      engine::register_reflection(registry).failed() ||
      engine::register_reflection(registry) != engine::result::invalid_argument ||
      gneiss::engine::meta::freeze(registry).failed() || engine::create_native(first).failed() ||
      engine::create_native(second).failed() || first->entity_create(entity).failed() ||
      first->entity_create(missing).failed() || first->node_create({}, entity, node).failed())
    return 1;
  const auto transform_type = engine::transform_property_type();
  const auto camera_type = engine::camera_property_type();
  gneiss::engine::meta::type_info info;
  if (gneiss::engine::meta::describe_type(registry, transform_type, info).failed() ||
      info.properties.size() != 3 ||
      gneiss::engine::meta::describe_type(registry, camera_type, info).failed() ||
      info.properties.size() != 4 ||
      gneiss::engine::meta::describe_type<engine::math::transform>(registry, info).failed() ||
      info.guid != transform_type ||
      gneiss::engine::meta::describe_type<engine::camera_properties>(registry, info).failed() ||
      info.guid != camera_type)
    return 2;
  gneiss::engine::value value;
  if (engine::read_component_property(registry, *first, entity.get(), transform_type,
                                      gneiss::engine::meta::property_id{1}, value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::vec3 ||
      std::abs(std::get<gneiss::engine::math::vector3>(value.payload).x) > 0.0001F)
    return 3;
  value.payload = gneiss::engine::math::vector3{2, 3, 4};
  if (engine::write_component_property(registry, *first, entity.get(), transform_type,
                                       gneiss::engine::meta::property_id{1}, value)
          .failed())
    return 4;
  engine::math::transform transform;
  if (first->entity_get_local_transform(entity, transform).failed() ||
      std::abs(transform.translation[0] - 2) > 0.0001F ||
      engine::read_component_property(registry, *first, missing.get(), transform_type,
                                      gneiss::engine::meta::property_id{1},
                                      value) != result::not_found ||
      engine::read_component_property(registry, *second, entity.get(), transform_type,
                                      gneiss::engine::meta::property_id{1},
                                      value) != result::invalid_handle)
    return 5;
  value.payload = gneiss::engine::math::vector3{0, 1, 1};
  if (engine::write_component_property(registry, *first, entity.get(), transform_type,
                                       gneiss::engine::meta::property_id{3},
                                       value) != result::invalid_argument ||
      first->entity_get_local_transform(entity, transform).failed() ||
      std::abs(transform.scale[0] - 1) > 0.0001F)
    return 6;
  engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (first->entity_set_camera(entity, camera, true).failed() ||
      engine::read_component_property(registry, *first, entity.get(), camera_type,
                                      gneiss::engine::meta::property_id{4}, value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::boolean || !std::get<bool>(value.payload))
    return 7;
  value.payload = 0.5F;
  if (engine::write_component_property(registry, *first, entity.get(), camera_type,
                                       gneiss::engine::meta::property_id{2}, value)
          .failed() ||
      first->entity_get_camera(entity, camera).failed() || std::abs(camera.near - 0.5F) > 0.0001F)
    return 8;
  value.payload = camera.far + 1;
  if (engine::write_component_property(registry, *first, entity.get(), camera_type,
                                       gneiss::engine::meta::property_id{2},
                                       value) != result::invalid_argument ||
      first->entity_get_camera(entity, camera).failed() || std::abs(camera.near - 0.5F) > 0.0001F ||
      engine::read_component_property(registry, *first, missing.get(), camera_type,
                                      gneiss::engine::meta::property_id{2},
                                      value) != result::not_found)
    return 9;
  result foreign_read{}, foreign_write{};
  std::thread worker([&] {
    gneiss::engine::value isolated;
    foreign_read = engine::read_component_property(registry, *first, entity.get(), transform_type,
                                                   gneiss::engine::meta::property_id{1}, isolated);
    isolated.payload = gneiss::engine::math::vector3{9, 9, 9};
    foreign_write =
        engine::write_component_property(registry, *first, entity.get(), transform_type,
                                         gneiss::engine::meta::property_id{1}, isolated);
  });
  worker.join();
  if (foreign_read != result::invalid_state || foreign_write != result::invalid_state ||
      first->entity_get_local_transform(entity, transform).failed() || transform.translation.x != 2)
    return 11;
  value.payload = false;
  if (engine::write_component_property(registry, *first, entity.get(), camera_type,
                                       gneiss::engine::meta::property_id{4},
                                       value) != result::unsupported)
    return 12;
  if (first->entity_destroy(entity).failed() ||
      engine::read_component_property(registry, *first, entity.get(), camera_type,
                                      gneiss::engine::meta::property_id{2},
                                      value) != result::invalid_handle)
    return 10;
  return 0;
}
