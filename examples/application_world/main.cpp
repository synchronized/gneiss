// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../support/scope_exit.hpp"
#include "scene_update.hpp"
#include <cstdio>
#include <gneiss/engine/scene.h>
int main() {
  example::scene_state state;
  gneiss_application app{};
  example::scope_exit close{[&] {
    if (app)
      (void)gneiss_application_destroy(app);
  }};
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = example::update;
  if (gneiss_application_create(&desc, &app) != GNEISS_SUCCESS)
    return 1;
  gneiss_world world{};
  gneiss_scene_node_id node{};
  if (gneiss_application_get_world(app, &world) != GNEISS_SUCCESS ||
      gneiss_world_entity_create(world, &state.entity) != GNEISS_SUCCESS ||
      gneiss_scene_node_create(world, 0, state.entity, &node) != GNEISS_SUCCESS)
    return 2;
  gneiss_transform initial = GNEISS_TRANSFORM_IDENTITY;
  initial.translation[0] = 1;
  initial.translation[1] = 2;
  initial.translation[2] = 3;
  if (gneiss_world_entity_set_local_transform(world, state.entity, &initial) != GNEISS_SUCCESS ||
      gneiss_application_run(app, 8) != GNEISS_SUCCESS)
    return 3;
  gneiss_transform final = GNEISS_TRANSFORM_IDENTITY;
  if (gneiss_world_entity_get_local_transform(world, state.entity, &final) != GNEISS_SUCCESS ||
      state.updates != 3 || final.translation[0] != 4 || final.translation[1] != 2 ||
      final.translation[2] != 3)
    return 4;
  std::printf("updates=%u, x=%g\n", state.updates, static_cast<double>(final.translation[0]));
  if (gneiss_application_destroy(app) != GNEISS_SUCCESS)
    return 5;
  app = 0;
  return 0;
}
