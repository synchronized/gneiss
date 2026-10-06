// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "scene_update.hpp"

namespace example {

gneiss::result update(gneiss::application_ref app, const gneiss::frame_time& /*time*/,
                      void* user_data) noexcept {
  auto& state = *static_cast<scene_state*>(user_data);
  gneiss::world_ref world;
  if (const auto status = app.get_world(world); status.failed()) {
    return status;
  }
  gneiss::transform transform;
  if (const auto status = world.get_local_transform(state.entity, transform); status.failed()) {
    return status;
  }
  // 固定步长便于观察示例结果；真实动画通常应使用 frame_time 的 delta_ns。
  transform.translation[0] += 1.0F;
  if (const auto status = world.set_local_transform(state.entity, transform); status.failed()) {
    return status;
  }
  ++state.updates;
  return state.updates == 3 ? app.request_exit() : gneiss::result::success;
}

} // namespace example
