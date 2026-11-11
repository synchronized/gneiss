// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/meta/transform_properties.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/meta_registration.hpp"
#include "engine/function/scene/components.hpp"
#include "engine/function/scene/world.hpp"
#include "engine/resource/scene/scene_description.hpp"

#include <array>
#include <cstdint>
#include <thread>
#include <type_traits>

static_assert(!std::is_convertible_v<gneiss::rid, gneiss::scene_instance_id>);
static_assert(!std::is_convertible_v<gneiss::scene_instance_id, gneiss::rid>);
static_assert(!std::is_convertible_v<gneiss::entity_id, gneiss::scene_instance_id>);

namespace {
bool scene_object_storage() {
  using namespace gneiss;
  using namespace gneiss;
  auto created = create_native();
  auto other = create_native();
  if (!created || !other)
    return false;
  auto& owner = **created;
  auto object_scene = owner.create_scene();
  if (!object_scene)
    return false;
  auto root = object_scene->create_object({"父节点", {}});
  if (!root)
    return false;
  auto child = object_scene->create_object({"子节点", {}}, *root);
  if (!child)
    return false;
  entity_id child_entity;
  if (gneiss::test_support::assign_value(child, child_entity).failed())
    return false;
  if (child->name() != "子节点" || child->set_name("新名称").failed() || child->name() != "新名称")
    return false;
  if (owner.find(child->entity()).try_component<gneiss::mesh_component>() != nullptr)
    return false;
  auto objects = object_scene->objects();
  auto roots = object_scene->roots();
  auto children = root->children();
  if (!objects || objects->size() != 2 || !roots || roots->size() != 1 || roots->front() != *root ||
      !children || children->size() != 1 || children->front() != *child)
    return false;
  if (child->try_component<mesh_component>() || child->add_component<mesh_component>().failed() ||
      !child->try_component<mesh_component>() ||
      child->add_component<mesh_component>() != error::invalid_state ||
      child->remove_component<mesh_component>().failed() ||
      child->try_component<mesh_component>() ||
      child->remove_component<mesh_component>() != error::not_found)
    return false;
  error add_from_worker;
  std::thread add_worker([&] { add_from_worker = child->add_component<mesh_component>(); });
  add_worker.join();
  if (add_from_worker != error::invalid_state)
    return false;
  math::transform invalid;
  invalid.scale.x = 0;
  auto failed = object_scene->create_object({"失败", invalid}, *root);
  auto count = owner.entity_count();
  if (failed || !count || *count != 2)
    return false;
  if ((*other)->find(root->entity()).destroy() != error::invalid_handle)
    return false;
  error foreign;
  std::thread worker([&] { foreign = (*root).destroy(); });
  worker.join();
  if (foreign != error::invalid_state)
    return false;
  if ((*root).destroy().failed())
    return false;
  count = owner.entity_count();
  if (!count || *count != 0 || owner.find(child_entity).is_valid())
    return false;
  if (children->front().is_valid() ||
      child->add_component<mesh_component>() != error::invalid_handle || child->is_valid() ||
      (*root).destroy() != error::invalid_handle)
    return false;
  auto replacement = object_scene->create_object({"新节点", {}});
  return replacement && *replacement != *root && *replacement != *child;
}

bool native_operations() {
  using namespace gneiss;
  gneiss::world_owner owner;
  if (gneiss::test_support::assign_value(gneiss::create_native(), owner) !=
      gneiss::error::success)
    return false;
  entity_id entity{};
  entity_id node{};
  std::uint64_t count{};
  auto object_scene = owner->create_scene();
  if (!object_scene)
    return false;
  auto object = object_scene->create_object({"object", {}});
  if (!object)
    return false;
  entity = object->entity();
  node = entity_id{entity.get()};
  if (!owner->find(entity).is_valid() ||
      gneiss::test_support::assign_value(owner->entity_count(), count) !=
          gneiss::error::success ||
      count != 1)
    return false;
  gneiss::math::transform local;
  local.translation = {2, 3, 4};
  gneiss::math::transform actual;
  if (owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()}).set_local_transform(local) !=
          gneiss::error::success ||
      gneiss::test_support::assign_value(
          owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()}).world_transform(),
          actual) != gneiss::error::success ||
      actual.translation != local.translation)
    return false;
  auto invalid = local;
  invalid.scale[0] = 0;
  if (owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()})
              .set_local_transform(invalid) != gneiss::error::invalid_argument ||
      gneiss::test_support::assign_value(
          owner->find(gneiss::entity_id{(gneiss::entity_id{node}).get()}).local_transform(),
          actual) != gneiss::error::success ||
      actual.scale[0] != 1)
    return false;
  actual.translation[0] = 42;
  if (gneiss::test_support::assign_value(
          owner->find(gneiss::entity_id{(gneiss::entity_id{0}).get()}).local_transform(), actual) !=
          gneiss::error::invalid_handle ||
      actual.translation[0] != 42)
    return false;
  gneiss::error foreign{};
  std::thread worker([&] { foreign = owner->find(gneiss::entity_id{entity}).destroy(); });
  worker.join();
  if (foreign != gneiss::error::invalid_state || !owner->find(entity).is_valid())
    return false;
  owner.reset();
  return !owner;
}

}

int main() {
  if (gneiss::initialize_meta().failed())
    return 22;
  if (!scene_object_storage())
    return 21;
  if (!native_operations())
    return 20;
  namespace service = gneiss;
  gneiss::world_owner world{};
  if (gneiss::test_support::assign_value(gneiss::create_native(), world) !=
      gneiss::error::success) {
    return 1;
  }
  auto scene = world->create_scene();
  if (!scene)
    return 2;
  auto created_object = scene->create_object();
  if (!created_object)
    return 2;
  const auto entity = created_object->entity().get();
  std::uint64_t count{};
  if (gneiss::test_support::assign_value(world->entity_count(), count) !=
          gneiss::error::success ||
      count != 1U) {
    return 3;
  }
  gneiss::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (world->find(gneiss::entity_id{entity})
          .add_component<gneiss::camera_component>(
              *gneiss::camera_component::create(camera)) !=
      gneiss::error::success) {
    return 4;
  }
  service::math::camera_settings settings;
  if (gneiss::test_support::read_component_value(
          world->find(gneiss::entity_id{entity}).try_component<gneiss::camera_component>(),
          settings) != gneiss::error::success ||
      settings.near != camera.near || settings.far != camera.far) {
    return 5;
  }
  settings.far = settings.near;
  if (world->find(gneiss::entity_id{entity})
              .try_component<gneiss::camera_component>()
              ->set_settings(settings) != gneiss::error::invalid_argument ||
      gneiss::test_support::read_component_value(
          world->find(gneiss::entity_id{entity}).try_component<gneiss::camera_component>(),
          camera) != gneiss::error::success ||
      camera.near == camera.far) {
    return 6;
  }
  gneiss::error wrong_thread{};
  if (gneiss::initialize_meta().failed())
    return 14;
  gneiss::math::transform candidate_transform;
  gneiss::value proposed;
  proposed.payload = gneiss::math::vector3{2, 3, 4};
  if (gneiss::meta::write_property(
          service::math::transform::type_guid, gneiss::meta::transform_translation_field,
          &candidate_transform, proposed) != gneiss::error::success ||
      candidate_transform.translation[0] != 2) {
    return 15;
  }
  proposed.payload = gneiss::math::vector3{0, 1, 1};
  if (gneiss::meta::write_property(
          service::math::transform::type_guid, gneiss::meta::transform_scale_field,
          &candidate_transform, proposed) != gneiss::error::invalid_argument ||
      candidate_transform.scale[0] != 1) {
    return 16;
  }
  service::camera_description candidate_camera{{1, 10, 20}, false};
  proposed.payload = 30.0F;
  if (gneiss::meta::write_property(
          gneiss::meta::type_of<service::math::camera_settings>().guid(),
          gneiss::meta::property_id{2}, &candidate_camera.settings,
          proposed) != gneiss::error::success) {
    return 17;
  }
  proposed.payload = 40.0F;
  if (gneiss::meta::write_property(
          gneiss::meta::type_of<service::math::camera_settings>().guid(),
          gneiss::meta::property_id{3}, &candidate_camera.settings,
          proposed) != gneiss::error::success ||
      candidate_camera.settings.near != 30 || candidate_camera.settings.far != 40) {
    return 18;
  }
  // 借用只在拥有者存活期使用；移动拥有者不改变对象地址。
  service::world_owner native;
  if (gneiss::test_support::assign_value(service::create_native(), native).failed())
    return 14;
  const auto* original = native.get();
  auto independent = service::create_native();
  if (!independent || independent->get() == original || native.get() != original)
    return 15;
  service::world_owner moved = std::move(native);
  if (native || moved.get() != original ||
      gneiss::test_support::assign_value(moved->entity_count(), count).failed())
    return 16;
  std::thread worker(
      [&] { wrong_thread = gneiss::test_support::assign_value(moved->entity_count(), count); });
  worker.join();
  if (wrong_thread != service::error::invalid_state)
    return 17;
  moved.reset();
  return 0;
}
