// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "scene_update.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
namespace example {
gneiss::engine::result update(gneiss::runtime::application& app, scene_state& state) noexcept {
  auto* world = app.native_world();
  gneiss::engine::transform transform;
  auto status = world->entity_get_local_transform(state.entity, transform);
  if (status.failed())
    return status;
  transform.translation[0] += 1.0F;
  status = world->entity_set_local_transform(state.entity, transform);
  if (status.failed())
    return status;
  if (++state.updates == 3)
    app.request_exit();
  return gneiss::engine::result::success;
}
}
