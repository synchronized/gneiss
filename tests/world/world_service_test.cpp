// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/component_identity.hpp"
#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/core/rid.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_reflection.hpp"
#include "engine/function/world/world_service.hpp"

#include <array>
#include <cstdint>
#include <thread>

namespace {
bool native_operations() {
  using namespace gneiss;
  gneiss::engine::world_owner owner;
  gneiss::engine::rid borrowed{};
  if (gneiss::engine::create_native(owner, borrowed) != gneiss::engine::result::success)
    return false;
  entity_id entity{};
  scene_node_id node{};
  bool alive{};
  std::uint64_t count{};
  if (owner->entity_create(entity) != gneiss::engine::result::success ||
      owner->node_create(gneiss::scene_node_id{0}, gneiss::entity_id{entity}, node) !=
          gneiss::engine::result::success ||
      owner->entity_is_alive(gneiss::entity_id{entity}, alive) != gneiss::engine::result::success ||
      !alive || gneiss::engine::entity_count(borrowed, count) != gneiss::engine::result::success ||
      count != 1)
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
  return gneiss::engine::entity_count(borrowed, count) == gneiss::engine::result::invalid_handle;
}

bool original_groups_visible(gneiss::engine::rid world,
                             const std::array<gneiss::engine::staged_object, 2>& old_objects,
                             const std::array<gneiss::engine::staged_object, 2>& new_objects) {
  namespace service = gneiss::engine;

  std::uint64_t count{};
  std::uint64_t active{};
  if (service::entity_count(world, count) != gneiss::engine::result::success || count != 2U ||
      service::get_active_camera(world, active) != gneiss::engine::result::success ||
      active != old_objects[0].entity.get()) {
    return false;
  }
  for (std::size_t index = 0; index < old_objects.size(); ++index) {
    std::uint8_t alive{};
    if (service::entity_is_alive(world, old_objects[index].entity.get(), alive) !=
            gneiss::engine::result::success ||
        alive == 0U ||
        service::entity_is_alive(world, new_objects[index].entity.get(), alive) !=
            gneiss::engine::result::success ||
        alive != 0U) {
      return false;
    }
  }
  return true;
}
bool batch_publication_test() {
  namespace service = gneiss::engine;
  gneiss::engine::rid world{};
  gneiss::engine::rid other{};
  if (service::create_world(world) != gneiss::engine::result::success ||
      service::create_world(other) != gneiss::engine::result::success) {
    return false;
  }
  std::array<gneiss::engine::publication, 2> previous;
  std::array<gneiss::engine::publication, 2> candidates;
  std::array<service::staged_object, 2> old_objects;
  std::array<service::staged_object, 2> new_objects;
  const service::camera_settings camera{
      .vertical_field_of_view_radians = 1.0F,
      .near_plane = 0.1F,
      .far_plane = 100.0F,
  };
  service::staged_object_description description;
  description.camera = &camera;
  for (std::size_t index = 0; index < previous.size(); ++index) {
    if (service::begin_publication(world, previous[index]) != gneiss::engine::result::success ||
        service::begin_publication(world, candidates[index]) != gneiss::engine::result::success ||
        service::create_staged_object(world, previous[index], description, old_objects[index]) !=
            gneiss::engine::result::success ||
        service::create_staged_object(world, candidates[index], description, new_objects[index]) !=
            gneiss::engine::result::success ||
        service::publish(world, previous[index]) != gneiss::engine::result::success) {
      return false;
    }
  }
  if (service::set_active_camera(world, old_objects[0].entity.get()) !=
      gneiss::engine::result::success) {
    return false;
  }
  const auto unchanged = [&] { return original_groups_visible(world, old_objects, new_objects); };
  // 后一个候选非法不得提前退休第一个旧组；同时验证空列表、重复和跨域。
  const std::array<gneiss::engine::publication, 2> invalid{candidates[0], {}};
  const std::array duplicates{candidates[0], candidates[0]};
  const std::array duplicate_previous{previous[0], previous[0]};
  gneiss::engine::publication foreign;
  if (service::begin_publication(other, foreign) != gneiss::engine::result::success) {
    return false;
  }
  const std::array mixed{candidates[0], foreign};
  if (service::replace_publications(world, {}, {}) != gneiss::engine::result::invalid_argument ||
      service::replace_publications(world, previous, invalid) !=
          gneiss::engine::result::invalid_state ||
      service::replace_publications(world, previous, duplicates) !=
          gneiss::engine::result::invalid_argument ||
      service::replace_publications(world, duplicate_previous, candidates) !=
          gneiss::engine::result::invalid_argument ||
      service::replace_publications(world, previous, mixed) !=
          gneiss::engine::result::invalid_state ||
      !unchanged()) {
    return false;
  }
  gneiss::engine::result wrong_thread{};
  std::thread worker(
      [&] { wrong_thread = service::replace_publications(world, previous, candidates); });
  worker.join();
  if (wrong_thread != gneiss::engine::result::invalid_state || !unchanged()) {
    return false;
  }
  // 旧组下存在其他组的隐藏孩子时拒绝整批替换，避免留下悬空父关系。
  gneiss::engine::publication external;
  service::staged_object child;
  description.parent = old_objects[1].node;
  if (service::begin_publication(world, external) != gneiss::engine::result::success ||
      service::create_staged_object(world, external, description, child) !=
          gneiss::engine::result::success ||
      service::replace_publications(world, previous, candidates) !=
          gneiss::engine::result::invalid_state ||
      !unchanged() ||
      service::destroy_staged_object(world, external, child) != gneiss::engine::result::success ||
      service::replace_publications(world, previous, candidates) !=
          gneiss::engine::result::success) {
    return false;
  }
  std::uint64_t count{};
  std::uint64_t active{};
  if (service::entity_count(world, count) != gneiss::engine::result::success || count != 2U ||
      service::get_active_camera(world, active) != gneiss::engine::result::not_ready ||
      active != 0U ||
      service::replace_publications(world, previous, candidates) !=
          gneiss::engine::result::invalid_state ||
      service::replace_publications(world, candidates, previous) !=
          gneiss::engine::result::invalid_state) {
    return false;
  }
  for (std::size_t index = 0; index < previous.size(); ++index) {
    std::uint8_t alive{};
    if (service::entity_is_alive(world, old_objects[index].entity.get(), alive) !=
            gneiss::engine::result::success ||
        alive != 0U ||
        service::entity_is_alive(world, new_objects[index].entity.get(), alive) !=
            gneiss::engine::result::success ||
        alive == 0U ||
        service::destroy_staged_object(world, previous[index], old_objects[index]) !=
            gneiss::engine::result::success ||
        service::destroy_staged_object(world, previous[index], old_objects[index]) !=
            gneiss::engine::result::invalid_handle) {
      return false;
    }
  }
  return service::destroy_world(world) == gneiss::engine::result::success &&
         service::destroy_world(other) == gneiss::engine::result::success;
}
}

int main() {
  if (!native_operations())
    return 20;
  if (!batch_publication_test()) {
    return 19;
  }
  namespace service = gneiss::engine;
  gneiss::engine::rid world{};
  if (gneiss::engine::create_world(world) != gneiss::engine::result::success) {
    return 1;
  }
  std::uint64_t entity{};
  if (service::entity_create(world, entity) != gneiss::engine::result::success) {
    return 2;
  }
  std::uint64_t count{};
  if (gneiss::engine::entity_count(world, count) != gneiss::engine::result::success ||
      count != 1U) {
    return 3;
  }
  gneiss::engine::camera_settings camera{1.04719755F, 0.1F, 1000.0F};
  if (gneiss::engine::entity_configure_camera(world, entity, camera) !=
      gneiss::engine::result::success) {
    return 4;
  }
  service::camera_settings settings;
  if (service::entity_get_camera(world, entity, settings) != gneiss::engine::result::success ||
      settings.near_plane != camera.near_plane || settings.far_plane != camera.far_plane) {
    return 5;
  }
  settings.far_plane = settings.near_plane;
  if (service::entity_configure_camera(world, entity, settings) !=
          gneiss::engine::result::invalid_argument ||
      gneiss::engine::entity_get_camera(world, entity, camera) != gneiss::engine::result::success ||
      camera.near_plane == camera.far_plane) {
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
      candidate_camera.settings.near_plane != 30 || candidate_camera.settings.far_plane != 40) {
    return 18;
  }
  gneiss::engine::publication group;
  service::staged_object root, child, failed;
  service::staged_object_description object;
  object.camera = &settings;
  settings.far_plane = 100.0F;
  if (service::begin_publication(world, group) != gneiss::engine::result::success ||
      service::create_staged_object(world, group, object, root) !=
          gneiss::engine::result::success ||
      service::entity_count(world, count) != gneiss::engine::result::success || count != 1U) {
    return 10;
  }
  object.parent = root.node;
  if (service::create_staged_object(world, group, object, child) !=
          gneiss::engine::result::success ||
      service::destroy_staged_object(world, group, root) != gneiss::engine::result::not_ready ||
      service::destroy_staged_object(world, group, {root.entity, child.node}) !=
          gneiss::engine::result::invalid_handle ||
      service::destroy_staged_object(world, group, child) != gneiss::engine::result::success ||
      service::destroy_staged_object(world, group, child) !=
          gneiss::engine::result::invalid_handle) {
    return 11;
  }
  object.transform.scale[0] = 0;
  if (service::create_staged_object(world, group, object, failed) !=
          gneiss::engine::result::invalid_argument ||
      failed.entity.is_valid() || failed.node.is_valid()) {
    return 12;
  }
  std::thread staging_worker([&] { wrong_thread = service::publish(world, group); });
  staging_worker.join();
  if (wrong_thread != gneiss::engine::result::invalid_state ||
      service::publish(world, group) != gneiss::engine::result::success ||
      service::entity_count(world, count) != gneiss::engine::result::success || count != 2U ||
      service::publish(world, group) != gneiss::engine::result::invalid_state ||
      service::destroy_staged_object(world, group, root) !=
          gneiss::engine::result::invalid_handle ||
      service::node_destroy(world, root.node.get()) != gneiss::engine::result::success ||
      service::entity_destroy(world, root.entity.get()) != gneiss::engine::result::success) {
    return 13;
  }
  std::thread worker([&] { wrong_thread = service::destroy_world(world); });
  worker.join();
  if (wrong_thread != gneiss::engine::result::invalid_state ||
      service::destroy_world(world) != gneiss::engine::result::success ||
      gneiss::engine::destroy_world(world) != gneiss::engine::result::invalid_handle) {
    return 7;
  }
  // C 和私有 C++ 入口使用同一个 generation 注册表，不能各自拥有一份 World。
  const auto stale = world;
  if (service::create_world(world) != gneiss::engine::result::success || world == stale ||
      service::entity_count(stale, count) != gneiss::engine::result::invalid_handle ||
      gneiss::engine::destroy_world(world) != gneiss::engine::result::success) {
    return 8;
  }
  // 原生拥有者析构自动撤销借用；移动不会使 RID 提前失效，也不授予借用方销毁权。
  service::world_owner native;
  gneiss::engine::rid borrowed{};
  if (service::create_native(native, borrowed) != gneiss::engine::result::success || !native ||
      gneiss::engine::destroy_world(borrowed) != gneiss::engine::result::invalid_state)
    return 14;
  const auto* original = native.get();
  gneiss::engine::rid rejected = borrowed;
  if (service::create_native(native, rejected) != gneiss::engine::result::invalid_state ||
      rejected != 0 || native.get() != original)
    return 15;
  service::world_owner moved = std::move(native);
  if (service::entity_count(borrowed, count) != gneiss::engine::result::success || count != 0)
    return 16;
  std::thread borrowed_worker([&] { wrong_thread = gneiss::engine::destroy_world(borrowed); });
  borrowed_worker.join();
  if (wrong_thread != gneiss::engine::result::invalid_state)
    return 17;
  moved.reset();
  if (service::entity_count(borrowed, count) != gneiss::engine::result::invalid_handle ||
      gneiss::engine::destroy_world(borrowed) != gneiss::engine::result::invalid_handle)
    return 18;
  if (service::create_native(native, rejected) != gneiss::engine::result::success ||
      rejected == borrowed ||
      service::entity_count(borrowed, count) != gneiss::engine::result::invalid_handle)
    return 19;
  native.reset();
  // 服务先校验相机参数；失败查询不改写输出。
  camera.far_plane = camera.near_plane;
  if (gneiss::engine::entity_configure_camera(stale, entity, camera) !=
          gneiss::engine::result::invalid_argument ||
      gneiss::engine::entity_get_camera(stale, entity, camera) !=
          gneiss::engine::result::invalid_handle ||
      camera.far_plane != camera.near_plane) {
    return 9;
  }
  return 0;
}
