// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/identifier_conversion.hpp"
#include "engine/core/reflection/component_identity.hpp"
#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"

#include <array>
#include <cstdint>
#include <thread>
#include <type_traits>

static_assert(!std::is_convertible_v<gneiss::engine::rid, gneiss::scene_instance_id>);
static_assert(!std::is_convertible_v<gneiss::scene_instance_id, gneiss::engine::rid>);
static_assert(!std::is_convertible_v<gneiss::scene_node_id, gneiss::scene_instance_id>);
static_assert(!std::is_convertible_v<gneiss::prefab_refresh_token, gneiss::scene_node_id>);

namespace {
bool native_operations() {
  using namespace gneiss;
  gneiss::engine::world_owner owner;
  if (gneiss::engine::create_native(owner) != gneiss::engine::result::success)
    return false;
  entity_id entity{};
  scene_node_id node{};
  bool alive{};
  std::uint64_t count{};
  if (owner->entity_create(entity) != gneiss::engine::result::success ||
      owner->node_create(gneiss::scene_node_id{0}, gneiss::entity_id{entity}, node) !=
          gneiss::engine::result::success ||
      owner->entity_is_alive(gneiss::entity_id{entity}, alive) != gneiss::engine::result::success ||
      !alive || owner->entity_count(count) != gneiss::engine::result::success || count != 1)
    return false;
  gneiss::engine::transform local;
  local.translation = {2, 3, 4};
  gneiss::engine::transform actual;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, local) !=
          gneiss::engine::result::success ||
      owner->node_get_world_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::result::success ||
      actual.translation != local.translation)
    return false;
  auto invalid = local;
  invalid.scale[0] = 0;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, invalid) !=
          gneiss::engine::result::invalid_argument ||
      owner->node_get_local_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::result::success ||
      actual.scale[0] != 1)
    return false;
  actual.translation[0] = 42;
  if (owner->node_get_local_transform(gneiss::scene_node_id{0}, actual) !=
          gneiss::engine::result::invalid_handle ||
      actual.translation[0] != 42)
    return false;
  gneiss::engine::result foreign{};
  std::thread worker([&] { foreign = owner->entity_destroy(gneiss::entity_id{entity}); });
  worker.join();
  if (foreign != gneiss::engine::result::invalid_state ||
      owner->entity_is_alive(gneiss::entity_id{entity}, alive) != gneiss::engine::result::success ||
      !alive)
    return false;
  gneiss::engine::publication group;
  gneiss::engine::staged_object candidate;
  if (owner->begin_publication(group) != gneiss::engine::result::success ||
      owner->create_staged_object(group, {}, candidate) != gneiss::engine::result::success ||
      owner->entity_count(count) != gneiss::engine::result::success || count != 1 ||
      owner->publish(group) != gneiss::engine::result::success ||
      owner->entity_count(count) != gneiss::engine::result::success || count != 2)
    return false;
  owner.reset();
  return !owner;
}

bool original_groups_visible(const gneiss::engine::world_owner& world,
                             const std::array<gneiss::engine::staged_object, 2>& old_objects,
                             const std::array<gneiss::engine::staged_object, 2>& new_objects) {
  namespace service = gneiss::engine;

  std::uint64_t count{};
  std::uint64_t active{};
  if (world->entity_count(count) != gneiss::engine::result::success || count != 2U ||
      gneiss::engine::write_identifier<gneiss::entity_id>(
          active, [&](auto& native_output) { return world->get_active_camera(native_output); }) !=
          gneiss::engine::result::success ||
      active != old_objects[0].entity.get()) {
    return false;
  }
  for (std::size_t index = 0; index < old_objects.size(); ++index) {
    bool alive{};
    if (world->entity_is_alive(gneiss::entity_id{old_objects[index].entity.get()}, alive) !=
            gneiss::engine::result::success ||
        alive == 0U ||
        world->entity_is_alive(gneiss::entity_id{new_objects[index].entity.get()}, alive) !=
            gneiss::engine::result::success ||
        alive != 0U) {
      return false;
    }
  }
  return true;
}
bool batch_publication_test() {
  namespace service = gneiss::engine;
  gneiss::engine::world_owner world{};
  gneiss::engine::world_owner other{};
  if (service::create_native(world) != gneiss::engine::result::success ||
      service::create_native(other) != gneiss::engine::result::success) {
    return false;
  }
  std::array<gneiss::engine::publication, 2> previous;
  std::array<gneiss::engine::publication, 2> candidates;
  std::array<service::staged_object, 2> old_objects;
  std::array<service::staged_object, 2> new_objects;
  const service::camera_settings camera{
      .fov = 1.0F,
      .near = 0.1F,
      .far = 100.0F,
  };
  service::staged_object_description description;
  description.camera = &camera;
  for (std::size_t index = 0; index < previous.size(); ++index) {
    if (world->begin_publication(previous[index]) != gneiss::engine::result::success ||
        world->begin_publication(candidates[index]) != gneiss::engine::result::success ||
        world->create_staged_object(previous[index], description, old_objects[index]) !=
            gneiss::engine::result::success ||
        world->create_staged_object(candidates[index], description, new_objects[index]) !=
            gneiss::engine::result::success ||
        world->publish(previous[index]) != gneiss::engine::result::success) {
      return false;
    }
  }
  if (world->set_active_camera(gneiss::entity_id{old_objects[0].entity.get()}) !=
      gneiss::engine::result::success) {
    return false;
  }
  const auto unchanged = [&] { return original_groups_visible(world, old_objects, new_objects); };
  // 后一个候选非法不得提前退休第一个旧组；同时验证空列表、重复和跨域。
  const std::array<gneiss::engine::publication, 2> invalid{candidates[0], {}};
  const std::array duplicates{candidates[0], candidates[0]};
  const std::array duplicate_previous{previous[0], previous[0]};
  gneiss::engine::publication foreign;
  if (other->begin_publication(foreign) != gneiss::engine::result::success) {
    return false;
  }
  const std::array mixed{candidates[0], foreign};
  if (world->replace_publications({}, {}) != gneiss::engine::result::invalid_argument ||
      world->replace_publications(previous, invalid) != gneiss::engine::result::invalid_state ||
      world->replace_publications(previous, duplicates) !=
          gneiss::engine::result::invalid_argument ||
      world->replace_publications(duplicate_previous, candidates) !=
          gneiss::engine::result::invalid_argument ||
      world->replace_publications(previous, mixed) != gneiss::engine::result::invalid_state ||
      !unchanged()) {
    return false;
  }
  gneiss::engine::result wrong_thread{};
  std::thread worker([&] { wrong_thread = world->replace_publications(previous, candidates); });
  worker.join();
  if (wrong_thread != gneiss::engine::result::invalid_state || !unchanged()) {
    return false;
  }
  // 旧组下存在其他组的隐藏孩子时拒绝整批替换，避免留下悬空父关系。
  gneiss::engine::publication external;
  service::staged_object child;
  description.parent = old_objects[1].node;
  if (world->begin_publication(external) != gneiss::engine::result::success ||
      world->create_staged_object(external, description, child) !=
          gneiss::engine::result::success ||
      world->replace_publications(previous, candidates) != gneiss::engine::result::invalid_state ||
      !unchanged() ||
      world->destroy_staged_object(external, child) != gneiss::engine::result::success ||
      world->replace_publications(previous, candidates) != gneiss::engine::result::success) {
    return false;
  }
  std::uint64_t count{};
  std::uint64_t active{};
  if (world->entity_count(count) != gneiss::engine::result::success || count != 2U ||
      gneiss::engine::write_identifier<gneiss::entity_id>(
          active, [&](auto& native_output) { return world->get_active_camera(native_output); }) !=
          gneiss::engine::result::not_ready ||
      active != 0U ||
      world->replace_publications(previous, candidates) != gneiss::engine::result::invalid_state ||
      world->replace_publications(candidates, previous) != gneiss::engine::result::invalid_state) {
    return false;
  }
  for (std::size_t index = 0; index < previous.size(); ++index) {
    bool alive{};
    if (world->entity_is_alive(gneiss::entity_id{old_objects[index].entity.get()}, alive) !=
            gneiss::engine::result::success ||
        alive != 0U ||
        world->entity_is_alive(gneiss::entity_id{new_objects[index].entity.get()}, alive) !=
            gneiss::engine::result::success ||
        alive == 0U ||
        world->destroy_staged_object(previous[index], old_objects[index]) !=
            gneiss::engine::result::success ||
        world->destroy_staged_object(previous[index], old_objects[index]) !=
            gneiss::engine::result::invalid_handle) {
      return false;
    }
  }
  return (world.reset(), gneiss::engine::result::success) == gneiss::engine::result::success &&
         (other.reset(), gneiss::engine::result::success) == gneiss::engine::result::success;
}
}

int main() {
  if (!native_operations())
    return 20;
  if (!batch_publication_test()) {
    return 19;
  }
  namespace service = gneiss::engine;
  gneiss::engine::world_owner world{};
  if (gneiss::engine::create_native(world) != gneiss::engine::result::success) {
    return 1;
  }
  std::uint64_t entity{};
  if (gneiss::engine::write_identifier<gneiss::entity_id>(entity, [&](auto& native_output) {
        return world->entity_create(native_output);
      }) != gneiss::engine::result::success) {
    return 2;
  }
  std::uint64_t count{};
  if (world->entity_count(count) != gneiss::engine::result::success || count != 1U) {
    return 3;
  }
  gneiss::engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (world->entity_configure_camera(gneiss::entity_id{entity}, camera) !=
      gneiss::engine::result::success) {
    return 4;
  }
  service::camera_settings settings;
  if (world->entity_get_camera(gneiss::entity_id{entity}, settings) !=
          gneiss::engine::result::success ||
      settings.near != camera.near || settings.far != camera.far) {
    return 5;
  }
  settings.far = settings.near;
  if (world->entity_configure_camera(gneiss::entity_id{entity}, settings) !=
          gneiss::engine::result::invalid_argument ||
      world->entity_get_camera(gneiss::entity_id{entity}, camera) !=
          gneiss::engine::result::success ||
      camera.near == camera.far) {
    return 6;
  }
  gneiss::engine::result wrong_thread{};
  gneiss::engine::registry_ref reflection;
  if (gneiss::engine::create_property_registry(reflection).failed())
    return 14;
  gneiss::engine::transform candidate_transform;
  gneiss::engine::value proposed;
  proposed.payload = gneiss::property_vec3{2, 3, 4};
  if (gneiss::engine::set_property(reflection, service::transform_property_type(),
                                   gneiss::engine::transform_translation_field.get(),
                                   &candidate_transform,
                                   proposed) != gneiss::engine::result::success ||
      candidate_transform.translation[0] != 2) {
    return 15;
  }
  proposed.payload = gneiss::property_vec3{0, 1, 1};
  if (gneiss::engine::set_property(reflection, service::transform_property_type(),
                                   gneiss::engine::transform_scale_field.get(),
                                   &candidate_transform,
                                   proposed) != gneiss::engine::result::invalid_argument ||
      candidate_transform.scale[0] != 1) {
    return 16;
  }
  service::camera_properties candidate_camera{{1, 10, 20}, false};
  proposed.payload = 30.0F;
  if (gneiss::engine::set_property(reflection, service::camera_property_type(),
                                   gneiss::engine::camera_near_plane_field.get(), &candidate_camera,
                                   proposed) != gneiss::engine::result::success) {
    return 17;
  }
  proposed.payload = 40.0F;
  if (gneiss::engine::set_property(reflection, service::camera_property_type(),
                                   gneiss::engine::camera_far_plane_field.get(), &candidate_camera,
                                   proposed) != gneiss::engine::result::success ||
      candidate_camera.settings.near != 30 || candidate_camera.settings.far != 40) {
    return 18;
  }
  gneiss::engine::publication group;
  service::staged_object root, child, failed;
  service::staged_object_description object;
  object.camera = &settings;
  settings.far = 100.0F;
  if (world->begin_publication(group) != gneiss::engine::result::success ||
      world->create_staged_object(group, object, root) != gneiss::engine::result::success ||
      world->entity_count(count) != gneiss::engine::result::success || count != 1U) {
    return 10;
  }
  object.parent = root.node;
  if (world->create_staged_object(group, object, child) != gneiss::engine::result::success ||
      world->destroy_staged_object(group, root) != gneiss::engine::result::not_ready ||
      world->destroy_staged_object(group, {root.entity, child.node}) !=
          gneiss::engine::result::invalid_handle ||
      world->destroy_staged_object(group, child) != gneiss::engine::result::success ||
      world->destroy_staged_object(group, child) != gneiss::engine::result::invalid_handle) {
    return 11;
  }
  object.transform.scale[0] = 0;
  if (world->create_staged_object(group, object, failed) !=
          gneiss::engine::result::invalid_argument ||
      failed.entity.is_valid() || failed.node.is_valid()) {
    return 12;
  }
  std::thread staging_worker([&] { wrong_thread = world->publish(group); });
  staging_worker.join();
  if (wrong_thread != gneiss::engine::result::invalid_state ||
      world->publish(group) != gneiss::engine::result::success ||
      world->entity_count(count) != gneiss::engine::result::success || count != 2U ||
      world->publish(group) != gneiss::engine::result::invalid_state ||
      world->destroy_staged_object(group, root) != gneiss::engine::result::invalid_handle ||
      world->node_destroy(gneiss::scene_node_id{root.node.get()}) !=
          gneiss::engine::result::success ||
      world->entity_destroy(gneiss::entity_id{root.entity.get()}) !=
          gneiss::engine::result::success) {
    return 13;
  }
  // 借用只在拥有者存活期使用；移动拥有者不改变对象地址。
  service::world_owner native;
  if (service::create_native(native).failed())
    return 14;
  const auto* original = native.get();
  if (service::create_native(native) != service::result::invalid_state || native.get() != original)
    return 15;
  service::world_owner moved = std::move(native);
  if (native || moved.get() != original || moved->entity_count(count).failed())
    return 16;
  std::thread worker([&] { wrong_thread = moved->entity_count(count); });
  worker.join();
  if (wrong_thread != service::result::invalid_state)
    return 17;
  moved.reset();
  return 0;
}
