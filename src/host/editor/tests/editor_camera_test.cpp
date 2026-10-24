// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/c_owner.hpp"
#include "engine/core/result.hpp"
#include "viewport/editor_camera.hpp"

#include <gneiss/engine/world.hpp>

#include <cmath>

using namespace gneiss;

namespace {

bool near(float lhs, float rhs) { return std::abs(lhs - rhs) < 1.0e-4F; }

} // namespace

int main() {
  gneiss::test_support::world_owner world;
  gneiss::editor::editor_camera camera;
  gneiss_entity_id active_camera = GNEISS_NULL_ENTITY_ID;
  if (([&] {
        const gneiss_world_desc desc = GNEISS_WORLD_DESC_INIT;
        return gneiss::engine::core::result{gneiss_world_create(&desc, world.put())};
      })() != gneiss::engine::core::result::success ||
      camera.initialize() != gneiss::engine::core::result::success || !camera.is_valid() ||
      gneiss::engine::core::result{gneiss_world_get_active_camera(world.get(), &active_camera)} !=
          gneiss::engine::core::result::not_ready ||
      active_camera != GNEISS_NULL_ENTITY_ID) {
    return 1;
  }
  std::uint64_t entity_count{};
  if (gneiss::engine::core::result{gneiss_world_entity_count(world.get(), &entity_count)} !=
          gneiss::engine::core::result::success ||
      entity_count != 0U) {
    return 8;
  }
  gneiss_entity_id scene_camera{};
  const gneiss_camera_desc settings = GNEISS_CAMERA_DESC_INIT;
  if (gneiss::engine::core::result{gneiss_world_entity_create(world.get(), &scene_camera)} !=
          gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_entity_configure_camera(
          world.get(), scene_camera, &settings)} != gneiss::engine::core::result::success ||
      gneiss::engine::core::result{gneiss_world_set_active_camera(world.get(), scene_camera)} !=
          gneiss::engine::core::result::success) {
    return 9;
  }
  const auto initial = camera.current_transform();
  gneiss::editor::editor_camera_input input;
  input.delta_seconds = 1.0F;
  input.move_forward = 1.0F;
  if (camera.update(input) != gneiss::engine::core::result::success ||
      near(camera.current_transform().translation[2], initial.translation[2])) {
    return 2;
  }
  gneiss::transform target{};
  target.translation[0] = 3.0F;
  target.translation[1] = 4.0F;
  target.translation[2] = 5.0F;
  const auto before_invalid_focus = camera.current_transform();
  if (camera.focus(target, -1.0F) != gneiss::engine::core::result::invalid_argument ||
      !near(camera.current_transform().translation[0], before_invalid_focus.translation[0]) ||
      camera.focus(target, 5.0F) != gneiss::engine::core::result::success) {
    return 3;
  }
  const auto focused = camera.current_transform();
  const auto offset_x = focused.translation[0] - target.translation[0];
  const auto offset_y = focused.translation[1] - target.translation[1];
  const auto offset_z = focused.translation[2] - target.translation[2];
  if (!near(std::sqrt((offset_x * offset_x) + (offset_y * offset_y) + (offset_z * offset_z)),
            5.0F)) {
    return 4;
  }
  // 连续拉远穿越多个网格层级，空闲帧也不能将相机拉回。
  input = {};
  for (int frame = 0; frame < 1200; ++frame) {
    const auto before = camera.current_transform();
    input.dolly = frame % 12 == 0 ? -1.0F : 0.0F;
    if (camera.update(input) != gneiss::engine::core::result::success) {
      return 6;
    }
    const auto after = camera.current_transform();
    if (after.translation[2] < before.translation[2] ||
        (input.dolly == 0.0F && (!near(after.translation[1], before.translation[1]) ||
                                 !near(after.translation[2], before.translation[2])))) {
      return 7;
    }
  }
  camera.shutdown();
  return camera.is_valid() ||
                 gneiss::engine::core::result{gneiss_world_get_active_camera(
                     world.get(), &active_camera)} != gneiss::engine::core::result::success ||
                 active_camera != scene_camera
             ? 5
             : 0;
}
