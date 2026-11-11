// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "scene_update.hpp"
#include "application/application.hpp"
#include "engine/function/scene/world.hpp"
namespace example {
gneiss::engine::error update(gneiss::runtime::application& app, scene_state& state) noexcept {
  auto* world = app.native_world();
  auto object = world->find(state.entity);
  auto transform = object.local_transform();
  if (!transform)
    return transform.error();
  transform->translation[0] += 1.0F;
  auto status = object.set_local_transform(*transform);
  if (status.failed())
    return status;
  if (++state.updates == 3)
    app.request_exit();
  return gneiss::engine::error::success;
}
}
