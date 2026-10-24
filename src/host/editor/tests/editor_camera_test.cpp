// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/result.hpp"
#include "engine/function/world/world.hpp"
#include "viewport/editor_camera.hpp"

#include "engine/core/reflection/reflection_types.hpp"
#include <gneiss/engine/world.h>

#include <cmath>

using namespace gneiss;

namespace {

bool near(float lhs, float rhs) { return std::abs(lhs - rhs) < 1.0e-4F; }

} // namespace

int main() {
  gneiss::engine::world_owner owner;
  gneiss_world world{};
  gneiss::editor::editor_camera camera;
  gneiss::entity_id active_camera = gneiss::entity_id{};
  if (gneiss::engine::create_native(owner, world) != gneiss::engine::result::success ||
      camera.initialize() != gneiss::engine::result::success || !camera.is_valid() ||
      gneiss::engine::result{owner->get_active_camera(active_camera)} !=
          gneiss::engine::result::not_ready ||
      active_camera != gneiss::entity_id{}) {
    return 1;
  }
  std::uint64_t entity_count{};
  if (gneiss::engine::result{owner->entity_count(entity_count)} !=
          gneiss::engine::result::success ||
      entity_count != 0U) {
    return 8;
  }
  gneiss::entity_id scene_camera{};
  const gneiss::engine::camera_settings settings{1.04719755F, 0.1F, 1000.0F};
  if (gneiss::engine::result{owner->entity_create(scene_camera)} !=
          gneiss::engine::result::success ||
      gneiss::engine::result{owner->entity_configure_camera(scene_camera, settings)} !=
          gneiss::engine::result::success ||
      gneiss::engine::result{owner->set_active_camera(scene_camera)} !=
          gneiss::engine::result::success) {
    return 9;
  }
  const auto initial = camera.current_transform();
  gneiss::editor::editor_camera_input input;
  input.delta_seconds = 1.0F;
  input.move_forward = 1.0F;
  if (camera.update(input) != gneiss::engine::result::success ||
      near(camera.current_transform().translation[2], initial.translation[2])) {
    return 2;
  }
  gneiss::engine::transform target{};
  target.translation[0] = 3.0F;
  target.translation[1] = 4.0F;
  target.translation[2] = 5.0F;
  const auto before_invalid_focus = camera.current_transform();
  if (camera.focus(target, -1.0F) != gneiss::engine::result::invalid_argument ||
      !near(camera.current_transform().translation[0], before_invalid_focus.translation[0]) ||
      camera.focus(target, 5.0F) != gneiss::engine::result::success) {
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
    if (camera.update(input) != gneiss::engine::result::success) {
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
                 gneiss::engine::result{owner->get_active_camera(active_camera)} !=
                     gneiss::engine::result::success ||
                 active_camera != scene_camera
             ? 5
             : 0;
}
