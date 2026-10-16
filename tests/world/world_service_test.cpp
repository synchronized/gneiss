// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/reflection/type_registry.hpp"
#include "engine/function/world/world_service.hpp"

#include <cstdint>
#include <thread>

int main() {
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
  gneiss_transform candidate_transform = GNEISS_TRANSFORM_IDENTITY;
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
