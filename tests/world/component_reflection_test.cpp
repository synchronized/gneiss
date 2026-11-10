// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/serialization/builtin_registration.hpp"
#include "engine/function/scene/component_identity.hpp"
#include "engine/function/scene/component_registration.hpp"
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/builtin_resource_registration.hpp"
#include <cmath>
#include <thread>

int main() {
  namespace engine = gneiss::engine;
  using engine::error;
  engine::world_owner first, second;
  gneiss::entity_id entity, missing;
  gneiss::entity_id node;
  if (gneiss::engine::serialization::register_builtin_types().failed() ||
      gneiss::engine::serialization::register_builtin_types() != engine::error::invalid_argument ||
      engine::register_builtin_resource_types().failed() ||
      gneiss::engine::meta::freeze().failed() ||
      gneiss::test_support::assign_value(engine::create_native(), first).failed() ||
      gneiss::test_support::assign_value(engine::create_native(), second).failed() ||
      first->entity_create(missing).failed())
    return 1;
  auto object_scene = first->create_scene();
  if (!object_scene)
    return 1;
  auto object = object_scene->create({{}, "object"});
  if (!object)
    return 1;
  entity = object->entity();
  node = gneiss::entity_id{entity.get()};
  const auto transform_type = engine::math::transform::type_guid;
  const auto camera_type = engine::camera_description::type_guid;
  gneiss::engine::meta::type info;
  if (!(info = gneiss::engine::meta::find_type(transform_type)) || info.properties().size() != 3 ||
      !(info = gneiss::engine::meta::find_type(camera_type)) || info.properties().size() != 3 ||
      !(info = gneiss::engine::meta::type_of<engine::math::transform>()) ||
      info.guid() != transform_type ||
      !(info = gneiss::engine::meta::type_of<engine::camera_description>()) ||
      info.guid() != camera_type)
    return 2;
  gneiss::engine::value value;
  if (gneiss::test_support::assign_value(
          engine::read_component_property(*first, entity.get(), transform_type,
                                          gneiss::engine::meta::property_id{1}),
          value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::vec3 ||
      std::abs(std::get<gneiss::engine::math::vector3>(value.payload).x) > 0.0001F)
    return 3;
  value.payload = gneiss::engine::math::vector3{2, 3, 4};
  if (engine::write_component_property(*first, entity.get(), transform_type,
                                       gneiss::engine::meta::property_id{1}, value)
          .failed())
    return 4;
  engine::math::transform transform;
  if (gneiss::test_support::assign_value(first->find(entity).local_transform(), transform)
          .failed() ||
      std::abs(transform.translation[0] - 2) > 0.0001F ||
      gneiss::test_support::assign_value(
          engine::read_component_property(*first, missing.get(), transform_type,
                                          gneiss::engine::meta::property_id{1}),
          value) != error::not_found ||
      gneiss::test_support::assign_value(
          engine::read_component_property(*second, entity.get(), transform_type,
                                          gneiss::engine::meta::property_id{1}),
          value) != error::invalid_handle)
    return 5;
  value.payload = gneiss::engine::math::vector3{0, 1, 1};
  if (engine::write_component_property(*first, entity.get(), transform_type,
                                       gneiss::engine::meta::property_id{3},
                                       value) != error::invalid_argument ||
      gneiss::test_support::assign_value(first->find(entity).local_transform(), transform)
          .failed() ||
      std::abs(transform.scale[0] - 1) > 0.0001F)
    return 6;
  engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (first->find(entity).set_component(gneiss::engine::camera_component{camera, true}).failed() ||
      first->set_active_camera(entity).failed() ||
      gneiss::test_support::assign_value(
          engine::read_component_property(*first, entity.get(), camera_type,
                                          gneiss::engine::meta::property_id{4}),
          value)
          .failed() ||
      value.kind() != gneiss::engine::value_kind::boolean || !std::get<bool>(value.payload))
    return 7;
  // 嵌套参数通过描述的反射视图编辑，不再使用另一份扁平相机类型。
  auto change_near = [&](float near) -> error {
    auto captured = engine::capture_components(*first, node);
    if (!captured)
      return captured.error();
    auto changed = engine::modify_component(
        *captured->find(camera_type), [&](engine::meta::instance root) -> error {
          auto settings = root.member(engine::meta::property_id{1});
          if (!settings)
            return settings.error();
          auto field = settings->member(engine::meta::property_id{2});
          return field ? field->assign(engine::value{near}) : field.error();
        });
    if (!changed)
      return changed.error();
    captured->set(*changed);
    return engine::install_components({*first->object(gneiss::entity_id{node.get()})}, *captured);
  };
  if (change_near(0.5F).failed() ||
      gneiss::test_support::read_component_value(
          first->find(entity).read_component<gneiss::engine::camera_component>(), camera)
          .failed() ||
      std::abs(camera.near - 0.5F) > 0.0001F)
    return 8;
  if (change_near(camera.far + 1) != error::invalid_argument ||
      gneiss::test_support::read_component_value(
          first->find(entity).read_component<gneiss::engine::camera_component>(), camera)
          .failed() ||
      std::abs(camera.near - 0.5F) > 0.0001F)
    return 9;
  error foreign_read{}, foreign_write{};
  std::thread worker([&] {
    gneiss::engine::value isolated;
    foreign_read = gneiss::test_support::assign_value(
        engine::read_component_property(*first, entity.get(), transform_type,
                                        gneiss::engine::meta::property_id{1}),
        isolated);
    isolated.payload = gneiss::engine::math::vector3{9, 9, 9};
    foreign_write = engine::write_component_property(
        *first, entity.get(), transform_type, gneiss::engine::meta::property_id{1}, isolated);
  });
  worker.join();
  if (foreign_read != error::invalid_state || foreign_write != error::invalid_state ||
      gneiss::test_support::assign_value(first->find(entity).local_transform(), transform)
          .failed() ||
      transform.translation.x != 2)
    return 11;
  value.payload = false;
  if (engine::write_component_property(*first, entity.get(), camera_type,
                                       gneiss::engine::meta::property_id{4},
                                       value) != error::unsupported)
    return 12;
  if (first->entity_destroy(entity).failed() ||
      gneiss::test_support::assign_value(
          engine::read_component_property(*first, entity.get(), camera_type,
                                          gneiss::engine::meta::property_id{2}),
          value) != error::invalid_handle)
    return 10;
  return 0;
}
