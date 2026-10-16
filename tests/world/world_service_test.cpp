// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"
#include "engine/function/world/world.hpp"
#include "engine/function/world/world_service.hpp"

#include <array>
#include <cstdint>
#include <thread>

namespace {
bool native_operations() {
  using namespace gneiss;
  world_internal::world_owner owner;
  gneiss_world borrowed{};
  if (world_internal::create_native(owner, borrowed) != GNEISS_SUCCESS)
    return false;
  gneiss_entity_id entity{};
  gneiss_scene_node_id node{};
  bool alive{};
  std::uint64_t count{};
  if (owner->entity_create(entity) != GNEISS_SUCCESS ||
      owner->node_create(0, entity, node) != GNEISS_SUCCESS ||
      owner->entity_is_alive(entity, alive) != GNEISS_SUCCESS || !alive ||
      gneiss_world_entity_count(borrowed, &count) != GNEISS_SUCCESS || count != 1)
    return false;
  engine::transform local;
  local.translation = {2, 3, 4};
  engine::transform actual;
  if (owner->node_set_local_transform(node, local) != GNEISS_SUCCESS ||
      owner->node_get_world_transform(node, actual) != GNEISS_SUCCESS ||
      actual.translation != local.translation)
    return false;
  auto invalid = local;
  invalid.scale[0] = 0;
  if (owner->node_set_local_transform(node, invalid) != GNEISS_ERROR_INVALID_ARGUMENT ||
      owner->node_get_local_transform(node, actual) != GNEISS_SUCCESS || actual.scale[0] != 1)
    return false;
  actual.translation[0] = 42;
  if (owner->node_get_local_transform(0, actual) != GNEISS_ERROR_INVALID_HANDLE ||
      actual.translation[0] != 42)
    return false;
  gneiss_result foreign{};
  std::thread worker([&] { foreign = owner->entity_destroy(entity); });
  worker.join();
  if (foreign != GNEISS_ERROR_INVALID_STATE ||
      owner->entity_is_alive(entity, alive) != GNEISS_SUCCESS || !alive)
    return false;
  core::publication group;
  world_internal::staged_object candidate;
  if (owner->begin_publication(group) != GNEISS_SUCCESS ||
      owner->create_staged_object(group, {}, candidate) != GNEISS_SUCCESS ||
      owner->entity_count(count) != GNEISS_SUCCESS || count != 1 ||
      owner->publish(group) != GNEISS_SUCCESS || owner->entity_count(count) != GNEISS_SUCCESS ||
      count != 2)
    return false;
  owner.reset();
  return gneiss_world_entity_count(borrowed, &count) == GNEISS_ERROR_INVALID_HANDLE;
}

bool original_groups_visible(
    gneiss_world world, const std::array<gneiss::world_internal::staged_object, 2>& old_objects,
    const std::array<gneiss::world_internal::staged_object, 2>& new_objects) {
  namespace service = gneiss::world_internal;

  std::uint64_t count{};
  gneiss_entity_id active{};
  if (service::entity_count(world, count) != GNEISS_SUCCESS || count != 2U ||
      service::get_active_camera(world, active) != GNEISS_SUCCESS ||
      active != old_objects[0].entity) {
    return false;
  }
  for (std::size_t index = 0; index < old_objects.size(); ++index) {
    std::uint8_t alive{};
    if (service::entity_is_alive(world, old_objects[index].entity, alive) != GNEISS_SUCCESS ||
        alive == 0U ||
        service::entity_is_alive(world, new_objects[index].entity, alive) != GNEISS_SUCCESS ||
        alive != 0U) {
      return false;
    }
  }
  return true;
}
bool batch_publication_test() {
  namespace service = gneiss::world_internal;
  gneiss_world world{};
  gneiss_world other{};
  if (service::create(world) != GNEISS_SUCCESS || service::create(other) != GNEISS_SUCCESS) {
    return false;
  }
  std::array<gneiss::core::publication, 2> previous;
  std::array<gneiss::core::publication, 2> candidates;
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
    if (service::begin_publication(world, previous[index]) != GNEISS_SUCCESS ||
        service::begin_publication(world, candidates[index]) != GNEISS_SUCCESS ||
        service::create_staged_object(world, previous[index], description, old_objects[index]) !=
            GNEISS_SUCCESS ||
        service::create_staged_object(world, candidates[index], description, new_objects[index]) !=
            GNEISS_SUCCESS ||
        service::publish(world, previous[index]) != GNEISS_SUCCESS) {
      return false;
    }
  }
  if (service::set_active_camera(world, old_objects[0].entity) != GNEISS_SUCCESS) {
    return false;
  }
  const auto unchanged = [&] { return original_groups_visible(world, old_objects, new_objects); };
  // 后一个候选非法不得提前退休第一个旧组；同时验证空列表、重复和跨域。
  const std::array<gneiss::core::publication, 2> invalid{candidates[0], {}};
  const std::array duplicates{candidates[0], candidates[0]};
  const std::array duplicate_previous{previous[0], previous[0]};
  gneiss::core::publication foreign;
  if (service::begin_publication(other, foreign) != GNEISS_SUCCESS) {
    return false;
  }
  const std::array mixed{candidates[0], foreign};
  if (service::replace_publications(world, {}, {}) != GNEISS_ERROR_INVALID_ARGUMENT ||
      service::replace_publications(world, previous, invalid) != GNEISS_ERROR_INVALID_STATE ||
      service::replace_publications(world, previous, duplicates) != GNEISS_ERROR_INVALID_ARGUMENT ||
      service::replace_publications(world, duplicate_previous, candidates) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      service::replace_publications(world, previous, mixed) != GNEISS_ERROR_INVALID_STATE ||
      !unchanged()) {
    return false;
  }
  gneiss_result wrong_thread{};
  std::thread worker(
      [&] { wrong_thread = service::replace_publications(world, previous, candidates); });
  worker.join();
  if (wrong_thread != GNEISS_ERROR_INVALID_STATE || !unchanged()) {
    return false;
  }
  // 旧组下存在其他组的隐藏孩子时拒绝整批替换，避免留下悬空父关系。
  gneiss::core::publication external;
  service::staged_object child;
  description.parent = old_objects[1].node;
  if (service::begin_publication(world, external) != GNEISS_SUCCESS ||
      service::create_staged_object(world, external, description, child) != GNEISS_SUCCESS ||
      service::replace_publications(world, previous, candidates) != GNEISS_ERROR_INVALID_STATE ||
      !unchanged() || service::destroy_staged_object(world, external, child) != GNEISS_SUCCESS ||
      service::replace_publications(world, previous, candidates) != GNEISS_SUCCESS) {
    return false;
  }
  std::uint64_t count{};
  gneiss_entity_id active{};
  if (service::entity_count(world, count) != GNEISS_SUCCESS || count != 2U ||
      service::get_active_camera(world, active) != GNEISS_ERROR_NOT_READY || active != 0U ||
      service::replace_publications(world, previous, candidates) != GNEISS_ERROR_INVALID_STATE ||
      service::replace_publications(world, candidates, previous) != GNEISS_ERROR_INVALID_STATE) {
    return false;
  }
  for (std::size_t index = 0; index < previous.size(); ++index) {
    std::uint8_t alive{};
    if (service::entity_is_alive(world, old_objects[index].entity, alive) != GNEISS_SUCCESS ||
        alive != 0U ||
        service::entity_is_alive(world, new_objects[index].entity, alive) != GNEISS_SUCCESS ||
        alive == 0U ||
        service::destroy_staged_object(world, previous[index], old_objects[index]) !=
            GNEISS_SUCCESS ||
        service::destroy_staged_object(world, previous[index], old_objects[index]) !=
            GNEISS_ERROR_INVALID_HANDLE) {
      return false;
    }
  }
  return service::destroy(world) == GNEISS_SUCCESS && service::destroy(other) == GNEISS_SUCCESS;
}
}

int main() {
  if (!native_operations())
    return 20;
  if (!batch_publication_test()) {
    return 19;
  }
  namespace service = gneiss::world_internal;
  gneiss_world world{};
  const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
  if (gneiss_world_create(&desc, &world) != GNEISS_SUCCESS) {
    return 1;
  }
  gneiss_entity_id entity{};
  if (service::entity_create(world, entity) != GNEISS_SUCCESS) {
    return 2;
  }
  std::uint64_t count{};
  if (gneiss_world_entity_count(world, &count) != GNEISS_SUCCESS || count != 1U) {
    return 3;
  }
  gneiss_camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  if (gneiss_world_entity_configure_camera(world, entity, &camera) != GNEISS_SUCCESS) {
    return 4;
  }
  service::camera_settings settings;
  if (service::entity_get_camera(world, entity, settings) != GNEISS_SUCCESS ||
      settings.near_plane != camera.near_plane || settings.far_plane != camera.far_plane) {
    return 5;
  }
  settings.far_plane = settings.near_plane;
  if (service::entity_configure_camera(world, entity, settings) != GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_world_entity_get_camera(world, entity, &camera) != GNEISS_SUCCESS ||
      camera.near_plane == camera.far_plane) {
    return 6;
  }
  gneiss_result wrong_thread{};
  gneiss_type_registry registry{};
  if (gneiss_type_registry_create(&registry) != GNEISS_SUCCESS ||
      service::register_reflection(registry) != GNEISS_SUCCESS ||
      gneiss_type_registry_freeze(registry) != GNEISS_SUCCESS) {
    return 14;
  }
  auto reflection = gneiss::reflection_internal::resolve(registry);
  gneiss::engine::transform candidate_transform;
  gneiss_property_value proposed = GNEISS_PROPERTY_VALUE_INIT;
  proposed.kind = GNEISS_PROPERTY_KIND_VEC3;
  proposed.payload.vec3_value = {2, 3, 4};
  if (gneiss::reflection_internal::set_candidate_property(
          reflection, service::transform_type_id(), GNEISS_TRANSFORM_FIELD_TRANSLATION,
          &candidate_transform, proposed) != GNEISS_SUCCESS ||
      candidate_transform.translation[0] != 2) {
    return 15;
  }
  proposed.payload.vec3_value = {0, 1, 1};
  if (gneiss::reflection_internal::set_candidate_property(
          reflection, service::transform_type_id(), GNEISS_TRANSFORM_FIELD_SCALE,
          &candidate_transform, proposed) != GNEISS_ERROR_INVALID_ARGUMENT ||
      candidate_transform.scale[0] != 1) {
    return 16;
  }
  service::camera_settings candidate_camera{1, 10, 20};
  proposed.kind = GNEISS_PROPERTY_KIND_FLOAT32;
  proposed.payload.float32_value = 30;
  if (gneiss::reflection_internal::set_candidate_property(
          reflection, service::camera_type_id(), GNEISS_CAMERA_FIELD_NEAR_PLANE, &candidate_camera,
          proposed) != GNEISS_SUCCESS) {
    return 17;
  }
  proposed.payload.float32_value = 40;
  if (gneiss::reflection_internal::set_candidate_property(
          reflection, service::camera_type_id(), GNEISS_CAMERA_FIELD_FAR_PLANE, &candidate_camera,
          proposed) != GNEISS_SUCCESS ||
      candidate_camera.near_plane != 30 || candidate_camera.far_plane != 40 ||
      gneiss_type_registry_destroy(registry) != GNEISS_SUCCESS) {
    return 18;
  }
  gneiss::core::publication group;
  service::staged_object root, child, failed;
  service::staged_object_description object;
  object.camera = &settings;
  settings.far_plane = 100.0F;
  if (service::begin_publication(world, group) != GNEISS_SUCCESS ||
      service::create_staged_object(world, group, object, root) != GNEISS_SUCCESS ||
      service::entity_count(world, count) != GNEISS_SUCCESS || count != 1U) {
    return 10;
  }
  object.parent = root.node;
  if (service::create_staged_object(world, group, object, child) != GNEISS_SUCCESS ||
      service::destroy_staged_object(world, group, root) != GNEISS_ERROR_NOT_READY ||
      service::destroy_staged_object(world, group, {root.entity, child.node}) !=
          GNEISS_ERROR_INVALID_HANDLE ||
      service::destroy_staged_object(world, group, child) != GNEISS_SUCCESS ||
      service::destroy_staged_object(world, group, child) != GNEISS_ERROR_INVALID_HANDLE) {
    return 11;
  }
  object.transform.scale[0] = 0;
  if (service::create_staged_object(world, group, object, failed) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      failed.entity != 0 || failed.node != 0) {
    return 12;
  }
  std::thread staging_worker([&] { wrong_thread = service::publish(world, group); });
  staging_worker.join();
  if (wrong_thread != GNEISS_ERROR_INVALID_STATE ||
      service::publish(world, group) != GNEISS_SUCCESS ||
      service::entity_count(world, count) != GNEISS_SUCCESS || count != 2U ||
      service::publish(world, group) != GNEISS_ERROR_INVALID_STATE ||
      service::destroy_staged_object(world, group, root) != GNEISS_ERROR_INVALID_HANDLE ||
      service::node_destroy(world, root.node) != GNEISS_SUCCESS ||
      service::entity_destroy(world, root.entity) != GNEISS_SUCCESS) {
    return 13;
  }
  std::thread worker([&] { wrong_thread = service::destroy(world); });
  worker.join();
  if (wrong_thread != GNEISS_ERROR_INVALID_STATE || service::destroy(world) != GNEISS_SUCCESS ||
      gneiss_world_destroy(world) != GNEISS_ERROR_INVALID_HANDLE) {
    return 7;
  }
  // C 和私有 C++ 入口使用同一个 generation 注册表，不能各自拥有一份 World。
  const auto stale = world;
  if (service::create(world) != GNEISS_SUCCESS || world == stale ||
      service::entity_count(stale, count) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_destroy(world) != GNEISS_SUCCESS) {
    return 8;
  }
  // 原生拥有者析构自动撤销借用；移动不会使 RID 提前失效，也不授予 C 销毁权。
  service::world_owner native;
  gneiss_world borrowed{};
  if (service::create_native(native, borrowed) != GNEISS_SUCCESS || !native ||
      gneiss_world_destroy(borrowed) != GNEISS_ERROR_INVALID_STATE)
    return 14;
  const auto* original = native.get();
  gneiss_world rejected = borrowed;
  if (service::create_native(native, rejected) != GNEISS_ERROR_INVALID_STATE || rejected != 0 ||
      native.get() != original)
    return 15;
  service::world_owner moved = std::move(native);
  if (service::entity_count(borrowed, count) != GNEISS_SUCCESS || count != 0)
    return 16;
  std::thread borrowed_worker([&] { wrong_thread = gneiss_world_destroy(borrowed); });
  borrowed_worker.join();
  if (wrong_thread != GNEISS_ERROR_INVALID_STATE)
    return 17;
  moved.reset();
  if (service::entity_count(borrowed, count) != GNEISS_ERROR_INVALID_HANDLE ||
      gneiss_world_destroy(borrowed) != GNEISS_ERROR_INVALID_HANDLE)
    return 18;
  if (service::create_native(native, rejected) != GNEISS_SUCCESS || rejected == borrowed ||
      service::entity_count(borrowed, count) != GNEISS_ERROR_INVALID_HANDLE)
    return 19;
  native.reset();
  // 即使句柄无效，相机参数错误仍优先报告；失败查询不改写输出。
  camera.far_plane = camera.near_plane;
  if (gneiss_world_entity_configure_camera(stale, entity, &camera) !=
          GNEISS_ERROR_INVALID_ARGUMENT ||
      gneiss_world_entity_get_camera(stale, entity, &camera) != GNEISS_ERROR_INVALID_HANDLE ||
      camera.far_plane != camera.near_plane) {
    return 9;
  }
  return 0;
}
