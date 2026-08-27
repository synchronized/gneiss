// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/gneiss.hpp>

#include <utility>

int main() {
  gneiss::world first;
  if (gneiss::world::create(first) != gneiss::result::success) {
    return 1;
  }
  gneiss::entity_id entity;
  if (first.create_entity(entity) != gneiss::result::success || !entity.is_valid()) {
    return 2;
  }
  bool is_alive = false;
  if (first.is_alive(entity, is_alive) != gneiss::result::success || !is_alive) {
    return 3;
  }
  gneiss::camera_desc camera = GNEISS_CAMERA_DESC_INIT;
  gneiss::camera_desc queried_camera = GNEISS_CAMERA_DESC_INIT;
  gneiss::entity_id active_camera;
  if (first.configure_camera(entity, camera) != gneiss::result::success ||
      first.get_camera(entity, queried_camera) != gneiss::result::success ||
      first.set_active_camera(entity) != gneiss::result::success ||
      first.get_active_camera(active_camera) != gneiss::result::success ||
      active_camera != entity) {
    return 4;
  }

  gneiss::world second{std::move(first)};
  // NOLINTNEXTLINE(bugprone-use-after-move): world 明确定义了可查询的移动后状态。
  if (first.is_valid() || !second.is_valid() ||
      second.destroy_entity(entity) != gneiss::result::success) {
    return 5;
  }
  return 0;
}
