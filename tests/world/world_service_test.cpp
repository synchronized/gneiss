// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "world/world_service.hpp"

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
