// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/value_result.hpp"
#include <gneiss/engine/core/meta/registration.hpp>
#include <gneiss/engine/core/meta/transform_properties.hpp>
#include <gneiss/engine/core/types/rid.hpp>
#include <gneiss/engine/core/serialization/builtin_registration.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/component_registration.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <gneiss/engine/resource/builtin_resource_registration.hpp>
#include <cmath>
#include <thread>

int main() {

  using gneiss::error;
  gneiss::world_owner first, second;
  gneiss::entity_id entity;
  gneiss::entity_id node;
  if (gneiss::serialization::register_builtin_types().failed() ||
      gneiss::serialization::register_builtin_types() != gneiss::error::invalid_argument ||
      gneiss::register_builtin_resource_types().failed() ||
      gneiss::meta::freeze().failed() ||
      gneiss::test_support::assign_value(gneiss::create_native(), first).failed() ||
      gneiss::test_support::assign_value(gneiss::create_native(), second).failed())
    return 1;
  auto object_scene = first->create_scene();
  if (!object_scene)
    return 1;
  auto object_state = object_scene->create_object({"object", {}});
  if (!object_state)
    return 1;
  auto object = *object_state;
  entity = object.entity();
  node = gneiss::entity_id{entity.get()};
  const auto transform_type = gneiss::math::transform::type_guid;
  const auto camera_type = gneiss::camera_description::type_guid;
  gneiss::meta::type info;
  if (!(info = gneiss::meta::find_type(transform_type)) || info.properties().size() != 3 ||
      !(info = gneiss::meta::find_type(camera_type)) || info.properties().size() != 3 ||
      !(info = gneiss::meta::type_of<gneiss::math::transform>()) ||
      info.guid() != transform_type ||
      !(info = gneiss::meta::type_of<gneiss::camera_description>()) ||
      info.guid() != camera_type)
    return 2;
  gneiss::value value;
  if (gneiss::test_support::assign_value(
          gneiss::read_component_property(*first, entity.get(), transform_type,
                                          gneiss::meta::property_id{1}),
          value)
          .failed() ||
      value.kind() != gneiss::value_kind::vec3 ||
      std::abs(std::get<gneiss::math::vector3>(value.payload).x) > 0.0001F)
    return 3;
  value.payload = gneiss::math::vector3{2, 3, 4};
  if (gneiss::write_component_property(*first, entity.get(), transform_type,
                                       gneiss::meta::property_id{1}, value)
          .failed())
    return 4;
  if (std::abs(object.local_transform().translation[0] - 2) > 0.0001F ||
      gneiss::test_support::assign_value(
          gneiss::read_component_property(*first, 0, transform_type,
                                          gneiss::meta::property_id{1}),
          value) != error::invalid_handle ||
      gneiss::test_support::assign_value(
          gneiss::read_component_property(*second, entity.get(), transform_type,
                                          gneiss::meta::property_id{1}),
          value) != error::invalid_handle)
    return 5;
  value.payload = gneiss::math::vector3{0, 1, 1};
  if (gneiss::write_component_property(*first, entity.get(), transform_type,
                                       gneiss::meta::property_id{3},
                                       value) != error::invalid_argument ||
      std::abs(object.local_transform().scale[0] - 1) > 0.0001F)
    return 6;
  gneiss::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (object.add_component<gneiss::camera_component>(camera, true).failed() ||
      gneiss::set_active_camera(*first, entity).failed() ||
      gneiss::test_support::assign_value(
          gneiss::read_component_property(*first, entity.get(), camera_type,
                                          gneiss::meta::property_id{4}),
          value)
          .failed() ||
      value.kind() != gneiss::value_kind::boolean || !std::get<bool>(value.payload))
    return 7;
  // 嵌套参数通过描述的反射视图编辑，不再使用另一份扁平相机类型。
  auto change_near = [&](float near) -> error {
    auto captured = gneiss::capture_components(*first, node);
    if (!captured)
      return captured.error();
    auto changed = gneiss::modify_component(
        *captured->find(camera_type), [&](gneiss::meta::instance root) -> error {
          auto settings = root.member(gneiss::meta::property_id{1});
          if (!settings)
            return settings.error();
          auto field = settings->member(gneiss::meta::property_id{2});
          return field ? field->assign(gneiss::value{near}) : field.error();
        });
    if (!changed)
      return changed.error();
    captured->set(*changed);
    return gneiss::replace_components({*first->object(gneiss::entity_id{node.get()})}, *captured);
  };
  if (change_near(0.5F).failed() ||
      gneiss::test_support::read_component_value(
          object.try_component<gneiss::camera_component>(), camera)
          .failed() ||
      std::abs(camera.near_plane - 0.5F) > 0.0001F)
    return 8;
  if (change_near(camera.far_plane + 1) != error::invalid_argument ||
      gneiss::test_support::read_component_value(
          object.try_component<gneiss::camera_component>(), camera)
          .failed() ||
      std::abs(camera.near_plane - 0.5F) > 0.0001F)
    return 9;
  error foreign_read{}, foreign_write{};
  std::thread worker([&] {
    gneiss::value isolated;
    foreign_read = gneiss::test_support::assign_value(
        gneiss::read_component_property(*first, entity.get(), transform_type,
                                        gneiss::meta::property_id{1}),
        isolated);
    isolated.payload = gneiss::math::vector3{9, 9, 9};
    foreign_write = gneiss::write_component_property(
        *first, entity.get(), transform_type, gneiss::meta::property_id{1}, isolated);
  });
  worker.join();
  if (foreign_read != error::invalid_state || foreign_write != error::invalid_state ||
      object.local_transform().translation.x != 2)
    return 11;
  value.payload = false;
  if (gneiss::write_component_property(*first, entity.get(), camera_type,
                                       gneiss::meta::property_id{4},
                                       value) != error::unsupported)
    return 12;
  object.destroy();
  if (gneiss::test_support::assign_value(
          gneiss::read_component_property(*first, entity.get(), camera_type,
                                          gneiss::meta::property_id{2}),
          value) != error::invalid_handle)
    return 10;
  return 0;
}
