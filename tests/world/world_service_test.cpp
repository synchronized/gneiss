// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"
#include "engine/core/result.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_service.hpp"

#include <array>
#include <cstdint>
#include <thread>

namespace {
bool native_operations() {
  using namespace gneiss;
  engine::world::world_owner owner;
  gneiss_world borrowed{};
  if (engine::world::create_native(owner, borrowed) != gneiss::engine::core::result::success)
    return false;
  entity_id entity{};
  scene_node_id node{};
  bool alive{};
  std::uint64_t count{};
  if (owner->entity_create(entity) != gneiss::engine::core::result::success ||
      owner->node_create(gneiss::scene_node_id{0}, gneiss::entity_id{entity}, node) !=
          gneiss::engine::core::result::success ||
      owner->entity_is_alive(gneiss::entity_id{entity}, alive) !=
          gneiss::engine::core::result::success ||
      !alive ||
      gneiss::engine::core::result{gneiss_world_entity_count(borrowed, &count)} !=
          gneiss::engine::core::result::success ||
      count != 1)
    return false;
  engine::core::math::transform local;
  local.translation = {2, 3, 4};
  engine::core::math::transform actual;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, local) !=
          gneiss::engine::core::result::success ||
      owner->node_get_world_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::core::result::success ||
      actual.translation != local.translation)
    return false;
  auto invalid = local;
  invalid.scale[0] = 0;
  if (owner->node_set_local_transform(gneiss::scene_node_id{node}, invalid) !=
          gneiss::engine::core::result::invalid_argument ||
      owner->node_get_local_transform(gneiss::scene_node_id{node}, actual) !=
          gneiss::engine::core::result::success ||
      actual.scale[0] != 1)
    return false;
  actual.translation[0] = 42;
  if (owner->node_get_local_transform(gneiss::scene_node_id{0}, actual) !=
          gneiss::engine::core::result::invalid_handle ||
      actual.translation[0] != 42)
    return false;
  gneiss::engine::core::result foreign{};
  std::thread worker([&] { foreign = owner->entity_destroy(gneiss::entity_id{entity}); });
  worker.join();
  if (foreign != gneiss::engine::core::result::invalid_state ||
      owner->entity_is_alive(gneiss::entity_id{entity}, alive) !=
          gneiss::engine::core::result::success ||
      !alive)
    return false;
  engine::core::publication group;
  engine::world::staged_object candidate;
  if (owner->begin_publication(group) != gneiss::engine::core::result::success ||
      owner->create_staged_object(group, {}, candidate) != gneiss::engine::core::result::success ||
      owner->entity_count(count) != gneiss::engine::core::result::success || count != 1 ||
      owner->publish(group) != gneiss::engine::core::result::success ||
      owner->entity_count(count) != gneiss::engine::core::result::success || count != 2)
    return false;
  owner.reset();
  return gneiss::engine::core::result{gneiss_world_entity_count(borrowed, &count)} ==
         gneiss::engine::core::result::invalid_handle;
}

bool original_groups_visible(
    gneiss_world world, const std::array<gneiss::engine::world::staged_object, 2>& old_objects,
    const std::array<gneiss::engine::world::staged_object, 2>& new_objects) {
  namespace service = gneiss::engine::world;

  std::uint64_t count{};
  gneiss_entity_id active{};
  if (service::entity_count(world, count) != gneiss::engine::core::result::success || count != 2U ||
      service::get_active_camera(world, active) != gneiss::engine::core::result::success ||
      active != old_objects[0].entity.get()) {
    return false;
  }
  for (std::size_t index = 0; index < old_objects.size(); ++index) {
    std::uint8_t alive{};
    if (service::entity_is_alive(world, old_objects[index].entity.get(), alive) !=
            gneiss::engine::core::result::success ||
        alive == 0U ||
        service::entity_is_alive(world, new_objects[index].entity.get(), alive) !=
            gneiss::engine::core::result::success ||
        alive != 0U) {
      return false;
    }
  }
  return true;
}
bool batch_publication_test() {
  namespace service = gneiss::engine::world;
  gneiss_world world{};
  gneiss_world other{};
  if (service::create(world) != gneiss::engine::core::result::success ||
      service::create(other) != gneiss::engine::core::result::success) {
    return false;
  }
  std::array<gneiss::engine::core::publication, 2> previous;
  std::array<gneiss::engine::core::publication, 2> candidates;
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
    if (service::begin_publication(world, previous[index]) !=
            gneiss::engine::core::result::success ||
        service::begin_publication(world, candidates[index]) !=
            gneiss::engine::core::result::success ||
        service::create_staged_object(world, previous[index], description, old_objects[index]) !=
            gneiss::engine::core::result::success ||
        service::create_staged_object(world, candidates[index], description, new_objects[index]) !=
            gneiss::engine::core::result::success ||
        service::publish(world, previous[index]) != gneiss::engine::core::result::success) {
      return false;
    }
  }
  if (service::set_active_camera(world, old_objects[0].entity.get()) !=
      gneiss::engine::core::result::success) {
    return false;
  }
  const auto unchanged = [&] { return original_groups_visible(world, old_objects, new_objects); };
  // 后一个候选非法不得提前退休第一个旧组；同时验证空列表、重复和跨域。
  const std::array<gneiss::engine::core::publication, 2> invalid{candidates[0], {}};
  const std::array duplicates{candidates[0], candidates[0]};
  const std::array duplicate_previous{previous[0], previous[0]};
  gneiss::engine::core::publication foreign;
  if (service::begin_publication(other, foreign) != gneiss::engine::core::result::success) {
    return false;
  }
  const std::array mixed{candidates[0], foreign};
  if (service::replace_publications(world, {}, {}) !=
          gneiss::engine::core::result::invalid_argument ||
      service::replace_publications(world, previous, invalid) !=
          gneiss::engine::core::result::invalid_state ||
      service::replace_publications(world, previous, duplicates) !=
          gneiss::engine::core::result::invalid_argument ||
      service::replace_publications(world, duplicate_previous, candidates) !=
          gneiss::engine::core::result::invalid_argument ||
      service::replace_publications(world, previous, mixed) !=
          gneiss::engine::core::result::invalid_state ||
      !unchanged()) {
    return false;
  }
  gneiss::engine::core::result wrong_thread{};
  std::thread worker(
      [&] { wrong_thread = service::replace_publications(world, previous, candidates); });
  worker.join();
  if (wrong_thread != gneiss::engine::core::result::invalid_state || !unchanged()) {
    return false;
  }
  // 旧组下存在其他组的隐藏孩子时拒绝整批替换，避免留下悬空父关系。
  gneiss::engine::core::publication external;
  service::staged_object child;
  description.parent = old_objects[1].node;
  if (service::begin_publication(world, external) != gneiss::engine::core::result::success ||
      service::create_staged_object(world, external, description, child) !=
          gneiss::engine::core::result::success ||
      service::replace_publications(world, previous, candidates) !=
          gneiss::engine::core::result::invalid_state ||
      !unchanged() ||
      service::destroy_staged_object(world, external, child) !=
          gneiss::engine::core::result::success ||
      service::replace_publications(world, previous, candidates) !=
          gneiss::engine::core::result::success) {
    return false;
  }
  std::uint64_t count{};
  gneiss_entity_id active{};
  if (service::entity_count(world, count) != gneiss::engine::core::result::success || count != 2U ||
      service::get_active_camera(world, active) != gneiss::engine::core::result::not_ready ||
      active != 0U ||
      service::replace_publications(world, previous, candidates) !=
          gneiss::engine::core::result::invalid_state ||
      service::replace_publications(world, candidates, previous) !=
          gneiss::engine::core::result::invalid_state) {
    return false;
  }
  for (std::size_t index = 0; index < previous.size(); ++index) {
    std::uint8_t alive{};
    if (service::entity_is_alive(world, old_objects[index].entity.get(), alive) !=
            gneiss::engine::core::result::success ||
        alive != 0U ||
        service::entity_is_alive(world, new_objects[index].entity.get(), alive) !=
            gneiss::engine::core::result::success ||
        alive == 0U ||
        service::destroy_staged_object(world, previous[index], old_objects[index]) !=
            gneiss::engine::core::result::success ||
        service::destroy_staged_object(world, previous[index], old_objects[index]) !=
            gneiss::engine::core::result::invalid_handle) {
      return false;
    }
  }
  return service::destroy(world) == gneiss::engine::core::result::success &&
         service::destroy(other) == gneiss::engine::core::result::success;
}
}

int main() {
  if (!native_operations())
    return 20;
  if (!batch_publication_test()) {
    return 19;
  }
  namespace service = gneiss::engine::world;
  gneiss_world world{};
  const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
  if (gneiss::engine::core::result{gneiss_world_create(&desc, &world)} !=
      gneiss::engine::core::result::success) {
    return 1;
  }
  gneiss_entity_id entity{};
  if (service::entity_create(world, entity) != gneiss::engine::core::result::success) {
    return 2;
  }
  std::uint64_t count{};
  if (gneiss::engine::core::result{gneiss_world_entity_count(world, &count)} !=
          gneiss::engine::core::result::success ||
      count != 1U) {
    return 3;
  }
  gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  if (gneiss::engine::core::result{gneiss_world_entity_configure_camera(world, entity, &camera)} !=
      gneiss::engine::core::result::success) {
    return 4;
  }
  service::camera_settings settings;
  if (service::entity_get_camera(world, entity, settings) !=
          gneiss::engine::core::result::success ||
      settings.near_plane != camera.near_plane || settings.far_plane != camera.far_plane) {
    return 5;
  }
  settings.far_plane = settings.near_plane;
  if (service::entity_configure_camera(world, entity, settings) !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::engine::core::result{gneiss_world_entity_get_camera(world, entity, &camera)} !=
          gneiss::engine::core::result::success ||
      camera.near_plane == camera.far_plane) {
    return 6;
  }
  gneiss::engine::core::result wrong_thread{};
  gneiss_type_registry registry{};
  if (gneiss::engine::core::result{gneiss_type_registry_create(&registry)} !=
          gneiss::engine::core::result::success ||
      service::register_reflection(registry) != gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_type_registry_freeze(registry)} !=
          gneiss::engine::core::result::success) {
    return 14;
  }
  auto reflection = gneiss::engine::core::reflection::resolve(registry);
  gneiss::engine::core::math::transform candidate_transform;
  gneiss_property_value proposed = GNEISS_PROPERTY_VALUE_INIT;
  proposed.kind = GNEISS_PROPERTY_KIND_VEC3;
  proposed.payload.vec3_value = {2, 3, 4};
  if (gneiss::engine::core::reflection::set_candidate_property(
          reflection, service::transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION,
          &candidate_transform, proposed) != gneiss::engine::core::result::success ||
      candidate_transform.translation[0] != 2) {
    return 15;
  }
  proposed.payload.vec3_value = {0, 1, 1};
  if (gneiss::engine::core::reflection::set_candidate_property(
          reflection, service::transform_type_id(), GNEISS_TRANSFORM_FIELD_SCALE,
          &candidate_transform, proposed) != gneiss::engine::core::result::invalid_argument ||
      candidate_transform.scale[0] != 1) {
    return 16;
  }
  service::camera_settings candidate_camera{1, 10, 20};
  proposed.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  proposed.payload.float32_value = 30;
  if (gneiss::engine::core::reflection::set_candidate_property(
          reflection, service::camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, &candidate_camera,
          proposed) != gneiss::engine::core::result::success) {
    return 17;
  }
  proposed.payload.float32_value = 40;
  if (gneiss::engine::core::reflection::set_candidate_property(
          reflection, service::camera_type_id(), GNEISS_CAMERA_FIELD_FAR_PLANE, &candidate_camera,
          proposed) != gneiss::engine::core::result::success ||
      candidate_camera.near_plane != 30 || candidate_camera.far_plane != 40 ||
      gneiss::engine::core::result{gneiss_type_registry_destroy(registry)} !=
          gneiss::engine::core::result::success) {
    return 18;
  }
  gneiss::engine::core::publication group;
  service::staged_object root, child, failed;
  service::staged_object_description object;
  object.camera = &settings;
  settings.far_plane = 100.0F;
  if (service::begin_publication(world, group) != gneiss::engine::core::result::success ||
      service::create_staged_object(world, group, object, root) !=
          gneiss::engine::core::result::success ||
      service::entity_count(world, count) != gneiss::engine::core::result::success || count != 1U) {
    return 10;
  }
  object.parent = root.node;
  if (service::create_staged_object(world, group, object, child) !=
          gneiss::engine::core::result::success ||
      service::destroy_staged_object(world, group, root) !=
          gneiss::engine::core::result::not_ready ||
      service::destroy_staged_object(world, group, {root.entity, child.node}) !=
          gneiss::engine::core::result::invalid_handle ||
      service::destroy_staged_object(world, group, child) !=
          gneiss::engine::core::result::success ||
      service::destroy_staged_object(world, group, child) !=
          gneiss::engine::core::result::invalid_handle) {
    return 11;
  }
  object.transform.scale[0] = 0;
  if (service::create_staged_object(world, group, object, failed) !=
          gneiss::engine::core::result::invalid_argument ||
      failed.entity.is_valid() || failed.node.is_valid()) {
    return 12;
  }
  std::thread staging_worker([&] { wrong_thread = service::publish(world, group); });
  staging_worker.join();
  if (wrong_thread != gneiss::engine::core::result::invalid_state ||
      service::publish(world, group) != gneiss::engine::core::result::success ||
      service::entity_count(world, count) != gneiss::engine::core::result::success || count != 2U ||
      service::publish(world, group) != gneiss::engine::core::result::invalid_state ||
      service::destroy_staged_object(world, group, root) !=
          gneiss::engine::core::result::invalid_handle ||
      service::node_destroy(world, root.node.get()) != gneiss::engine::core::result::success ||
      service::entity_destroy(world, root.entity.get()) != gneiss::engine::core::result::success) {
    return 13;
  }
  std::thread worker([&] { wrong_thread = service::destroy(world); });
  worker.join();
  if (wrong_thread != gneiss::engine::core::result::invalid_state ||
      service::destroy(world) != gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_destroy(world)} !=
          gneiss::engine::core::result::invalid_handle) {
    return 7;
  }
  // C 和私有 C++ 入口使用同一个 generation 注册表，不能各自拥有一份 World。
  const auto stale = world;
  if (service::create(world) != gneiss::engine::core::result::success || world == stale ||
      service::entity_count(stale, count) != gneiss::engine::core::result::invalid_handle ||
      gneiss::engine::core::result{gneiss_world_destroy(world)} !=
          gneiss::engine::core::result::success) {
    return 8;
  }
  // 原生拥有者析构自动撤销借用；移动不会使 RID 提前失效，也不授予 C 销毁权。
  service::world_owner native;
  gneiss_world borrowed{};
  if (service::create_native(native, borrowed) != gneiss::engine::core::result::success ||
      !native ||
      gneiss::engine::core::result{gneiss_world_destroy(borrowed)} !=
          gneiss::engine::core::result::invalid_state)
    return 14;
  const auto* original = native.get();
  gneiss_world rejected = borrowed;
  if (service::create_native(native, rejected) != gneiss::engine::core::result::invalid_state ||
      rejected != 0 || native.get() != original)
    return 15;
  service::world_owner moved = std::move(native);
  if (service::entity_count(borrowed, count) != gneiss::engine::core::result::success || count != 0)
    return 16;
  std::thread borrowed_worker(
      [&] { wrong_thread = gneiss::engine::core::result{gneiss_world_destroy(borrowed)}; });
  borrowed_worker.join();
  if (wrong_thread != gneiss::engine::core::result::invalid_state)
    return 17;
  moved.reset();
  if (service::entity_count(borrowed, count) != gneiss::engine::core::result::invalid_handle ||
      gneiss::engine::core::result{gneiss_world_destroy(borrowed)} !=
          gneiss::engine::core::result::invalid_handle)
    return 18;
  if (service::create_native(native, rejected) != gneiss::engine::core::result::success ||
      rejected == borrowed ||
      service::entity_count(borrowed, count) != gneiss::engine::core::result::invalid_handle)
    return 19;
  native.reset();
  // 即使句柄无效，相机参数错误仍优先报告；失败查询不改写输出。
  camera.far_plane = camera.near_plane;
  if (gneiss::engine::core::result{gneiss_world_entity_configure_camera(stale, entity, &camera)} !=
          gneiss::engine::core::result::invalid_argument ||
      gneiss::engine::core::result{gneiss_world_entity_get_camera(stale, entity, &camera)} !=
          gneiss::engine::core::result::invalid_handle ||
      camera.far_plane != camera.near_plane) {
    return 9;
  }
  return 0;
}
