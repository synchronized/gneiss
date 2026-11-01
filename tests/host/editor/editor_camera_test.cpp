// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../support/value_result.hpp"
#include "engine/core/error.hpp"
#include "engine/function/world/world.hpp"
#include "viewport/editor_camera.hpp"

#include "engine/core/meta/type.hpp"
#include "engine/core/rid.hpp"

#include <cmath>

using namespace gneiss;

namespace {

bool near(float lhs, float rhs) { return std::abs(lhs - rhs) < 1.0e-4F; }

} // namespace

int main() {
  gneiss::engine::world_owner owner;

  gneiss::editor::editor_camera camera;
  gneiss::entity_id active_camera = gneiss::entity_id{};
  if (gneiss::test_support::assign_value(gneiss::engine::create_native(), owner) !=
          gneiss::engine::error::success ||
      camera.initialize() != gneiss::engine::error::success || !camera.is_valid() ||
      gneiss::engine::error{owner->get_active_camera(active_camera)} !=
          gneiss::engine::error::not_ready ||
      active_camera != gneiss::entity_id{}) {
    return 1;
  }
  std::uint64_t entity_count{};
  if (gneiss::engine::error{gneiss::test_support::assign_value(
          owner->entity_count(), entity_count)} != gneiss::engine::error::success ||
      entity_count != 0U) {
    return 8;
  }
  gneiss::entity_id scene_camera{};
  const gneiss::engine::math::camera_settings settings{1.04719755F, 0.1F, 1000.0F};
  if (gneiss::engine::error{owner->entity_create(scene_camera)} != gneiss::engine::error::success ||
      gneiss::engine::error{owner->entity_configure_camera(scene_camera, settings)} !=
          gneiss::engine::error::success ||
      gneiss::engine::error{owner->set_active_camera(scene_camera)} !=
          gneiss::engine::error::success) {
    return 9;
  }
  const auto initial = camera.current_transform();
  gneiss::editor::editor_camera_input input;
  input.delta_seconds = 1.0F;
  input.move_forward = 1.0F;
  if (camera.update(input) != gneiss::engine::error::success ||
      near(camera.current_transform().translation[2], initial.translation[2])) {
    return 2;
  }
  gneiss::engine::math::transform target{};
  target.translation[0] = 3.0F;
  target.translation[1] = 4.0F;
  target.translation[2] = 5.0F;
  const auto before_invalid_focus = camera.current_transform();
  if (camera.focus(target, -1.0F) != gneiss::engine::error::invalid_argument ||
      !near(camera.current_transform().translation[0], before_invalid_focus.translation[0]) ||
      camera.focus(target, 5.0F) != gneiss::engine::error::success) {
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
    if (camera.update(input) != gneiss::engine::error::success) {
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
                 gneiss::engine::error{owner->get_active_camera(active_camera)} !=
                     gneiss::engine::error::success ||
                 active_camera != scene_camera
             ? 5
             : 0;
}
