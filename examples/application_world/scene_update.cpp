// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "scene_update.hpp"
namespace example {
gneiss_result update(gneiss_application app, const gneiss_frame_time*, void* user_data) noexcept {
  auto& state = *static_cast<scene_state*>(user_data);
  gneiss_world world{};
  auto status = gneiss_application_get_world(app, &world);
  if (status != GNEISS_SUCCESS)
    return status;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  status = gneiss_world_entity_get_local_transform(world, state.entity, &transform);
  if (status != GNEISS_SUCCESS)
    return status;
  transform.translation[0] += 1.0F;
  status = gneiss_world_entity_set_local_transform(world, state.entity, &transform);
  if (status != GNEISS_SUCCESS)
    return status;
  ++state.updates;
  return state.updates == 3 ? gneiss_application_request_exit(app) : GNEISS_SUCCESS;
}
}
