// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_camera.h"

#include <gneiss/world.hpp>

#include <cmath>

namespace {

bool near(float lhs, float rhs) { return std::abs(lhs - rhs) < 1.0e-4F; }

} // namespace

int main() {
  gneiss::world world;
  gneiss::editor::editor_camera camera;
  gneiss_entity_id active_camera = GNEISS_NULL_ENTITY_ID;
  if (gneiss::world::create(world) != gneiss::result::success ||
      camera.initialize(world.get()) != gneiss::result::success || !camera.is_valid() ||
      gneiss_world_get_active_camera(world.get(), &active_camera) != GNEISS_SUCCESS ||
      active_camera == GNEISS_NULL_ENTITY_ID) {
    return 1;
  }
  const auto initial = camera.current_transform();
  gneiss::editor::editor_camera_input input;
  input.delta_seconds = 1.0F;
  input.move_forward = 1.0F;
  if (camera.update(input) != gneiss::result::success ||
      near(camera.current_transform().translation[2], initial.translation[2])) {
    return 2;
  }
  gneiss::transform target = GNEISS_TRANSFORM_IDENTITY;
  target.translation[0] = 3.0F;
  target.translation[1] = 4.0F;
  target.translation[2] = 5.0F;
  const auto before_invalid_focus = camera.current_transform();
  if (camera.focus(target, -1.0F) != gneiss::result::invalid_argument ||
      !near(camera.current_transform().translation[0], before_invalid_focus.translation[0]) ||
      camera.focus(target, 5.0F) != gneiss::result::success) {
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
  camera.shutdown();
  return camera.is_valid() || gneiss_world_get_active_camera(world.get(), &active_camera) !=
                                  GNEISS_ERROR_NOT_READY
             ? 5
             : 0;
}
