// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/core/identifier_conversion.hpp"
#include "engine/core/meta/registration.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/component_identity.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <array>
#include <cstdint>
#include <thread>
#include <type_traits>

static_assert(!std::is_convertible_v<gneiss::engine::rid, gneiss::scene_instance_id>);
static_assert(!std::is_convertible_v<gneiss::scene_instance_id, gneiss::engine::rid>);
static_assert(!std::is_convertible_v<gneiss::scene_node_id, gneiss::scene_instance_id>);

namespace {
bool scene_object_storage() {
  using namespace gneiss;
  using namespace gneiss::engine;
  auto created = create_native();
  auto other = create_native();
  if (!created || !other)
    return false;
  auto& owner = **created;
  auto root = owner.object_create({}, {"root", "父节点"}, {});
  if (!root)
    return false;
  auto child = owner.object_create(*root, {"child", "子节点"}, {});
  if (!child)
    return false;
  entity_id child_entity;
  if (owner.node_get_entity(*child, child_entity).failed())
    return false;
  auto identity = owner.node_identity(*child);
  if (!identity || identity->uuid != "child" || identity->name != "子节点")
    return false;
  if (owner.node_set_identity(*child, {"child", "新名称"}).failed())
    return false;
  auto renamed = owner.node_identity(*child);
  if (!renamed || renamed->name != "新名称")
    return false;
  if (owner.node_mesh_renderer(*child).error() != error::not_found)
    return false;
  math::transform invalid;
  invalid.scale.x = 0;
  auto failed = owner.object_create(*root, {"failed", "失败"}, invalid);
  auto count = owner.entity_count();
  if (failed || !count || *count != 2)
    return false;
  if ((*other)->object_destroy(*root) != error::invalid_handle)
    return false;
  error foreign;
  std::thread worker([&] { foreign = owner.object_destroy(*root); });
  worker.join();
  if (foreign != error::invalid_state)
    return false;
  if (owner.object_destroy(*root).failed())
    return false;
  count = owner.entity_count();
  auto alive = owner.entity_is_alive(child_entity);
  if (!count || *count != 0 || !alive || *alive)
    return false;
  if (owner.node_identity(*child).error() != error::invalid_handle ||
      owner.object_destroy(*root) != error::invalid_handle)
    return false;
  auto replacement = owner.object_create({}, {"new", "新节点"}, {});
  return replacement && *replacement != *root && *replacement != *child;
}

bool native_operations() {
  using namespace gneiss;
  gneiss::engine::world_owner owner;
  if (gneiss::test_support::assign_value(gneiss::engine::create_native(), owner) !=
      gneiss::engine::error::success)
    return false;
  entity_id entity{};
  scene_node_id node{};
  bool alive{};
  std::uint64_t count{};
  if (owner->entity_create(entity) != gneiss::engine::error::success ||
      owner->node_create(gneiss::scene_node_id{0}, gneiss::entity_id{entity}, node) !=
          gneiss::engine::error::success ||
      gneiss::test_support::assign_value(owner->entity_is_alive(gneiss::entity_id{entity}),
                                         alive) != gneiss::engine::error::success ||
      !alive ||
      gneiss::test_support::assign_value(owner->entity_count(), count) !=
          gneiss::engine::error::success ||
      count != 1)
    return false;
  gneiss::engine::math::transform local;
  local.translation = {2, 3, 4};
  gneiss::engine::math::transform actual;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, local) !=
          gneiss::engine::error::success ||
      owner->node_get_world_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::error::success ||
      actual.translation != local.translation)
    return false;
  auto invalid = local;
  invalid.scale[0] = 0;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, invalid) !=
          gneiss::engine::error::invalid_argument ||
      owner->node_get_local_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::error::success ||
      actual.scale[0] != 1)
    return false;
  actual.translation[0] = 42;
  if (owner->node_get_local_transform(gneiss::scene_node_id{0}, actual) !=
          gneiss::engine::error::invalid_handle ||
      actual.translation[0] != 42)
    return false;
  gneiss::engine::error foreign{};
  std::thread worker([&] { foreign = owner->entity_destroy(gneiss::entity_id{entity}); });
  worker.join();
  if (foreign != gneiss::engine::error::invalid_state ||
      gneiss::test_support::assign_value(owner->entity_is_alive(gneiss::entity_id{entity}),
                                         alive) != gneiss::engine::error::success ||
      !alive)
    return false;
  owner.reset();
  return !owner;
}

}

int main() {
  if (!scene_object_storage())
    return 21;
  if (!native_operations())
    return 20;
  namespace service = gneiss::engine;
  gneiss::engine::world_owner world{};
  if (gneiss::test_support::assign_value(gneiss::engine::create_native(), world) !=
      gneiss::engine::error::success) {
    return 1;
  }
  std::uint64_t entity{};
  if (gneiss::engine::write_identifier<gneiss::entity_id>(entity, [&](auto& native_output) {
        return world->entity_create(native_output);
      }) != gneiss::engine::error::success) {
    return 2;
  }
  std::uint64_t count{};
  if (gneiss::test_support::assign_value(world->entity_count(), count) !=
          gneiss::engine::error::success ||
      count != 1U) {
    return 3;
  }
  gneiss::engine::math::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (world->entity_configure_camera(gneiss::entity_id{entity}, camera) !=
      gneiss::engine::error::success) {
    return 4;
  }
  service::math::camera_settings settings;
  if (world->entity_get_camera(gneiss::entity_id{entity}, settings) !=
          gneiss::engine::error::success ||
      settings.near != camera.near || settings.far != camera.far) {
    return 5;
  }
  settings.far = settings.near;
  if (world->entity_configure_camera(gneiss::entity_id{entity}, settings) !=
          gneiss::engine::error::invalid_argument ||
      world->entity_get_camera(gneiss::entity_id{entity}, camera) !=
          gneiss::engine::error::success ||
      camera.near == camera.far) {
    return 6;
  }
  gneiss::engine::error wrong_thread{};
  gneiss::engine::meta::registry_ref reflection;
  if (gneiss::test_support::assign_value(gneiss::engine::create_property_registry(), reflection)
          .failed())
    return 14;
  gneiss::engine::math::transform candidate_transform;
  gneiss::engine::value proposed;
  proposed.payload = gneiss::engine::math::vector3{2, 3, 4};
  if (gneiss::engine::meta::write_property(reflection, service::transform_property_type(),
                                           gneiss::engine::transform_translation_field,
                                           &candidate_transform,
                                           proposed) != gneiss::engine::error::success ||
      candidate_transform.translation[0] != 2) {
    return 15;
  }
  proposed.payload = gneiss::engine::math::vector3{0, 1, 1};
  if (gneiss::engine::meta::write_property(
          reflection, service::transform_property_type(), gneiss::engine::transform_scale_field,
          &candidate_transform, proposed) != gneiss::engine::error::invalid_argument ||
      candidate_transform.scale[0] != 1) {
    return 16;
  }
  service::camera_properties candidate_camera{{1, 10, 20}, false};
  proposed.payload = 30.0F;
  if (gneiss::engine::meta::write_property(
          reflection, service::camera_property_type(), gneiss::engine::camera_near_plane_field,
          &candidate_camera, proposed) != gneiss::engine::error::success) {
    return 17;
  }
  proposed.payload = 40.0F;
  if (gneiss::engine::meta::write_property(
          reflection, service::camera_property_type(), gneiss::engine::camera_far_plane_field,
          &candidate_camera, proposed) != gneiss::engine::error::success ||
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
