// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"
#include "scene_update.hpp"
#include <cstdio>
int main() {
  example::scene_state state;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  desc.callbacks.update = [&](const auto&) { return example::update(*app, state); };
  if (gneiss::runtime::application::create(desc, app).failed())
    return 1;
  auto* world = app->native_world();
  gneiss::scene_node_id node;
  if (world->entity_create(state.entity).failed() ||
      world->node_create({}, state.entity, node).failed())
    return 2;
  gneiss::engine::transform initial;
  initial.translation = {1, 2, 3};
  if (world->entity_set_local_transform(state.entity, initial).failed() || app->run(8).failed())
    return 3;
  gneiss::engine::transform final;
  if (world->entity_get_local_transform(state.entity, final).failed() || state.updates != 3 ||
      final.translation[0] != 4 || final.translation[1] != 2 || final.translation[2] != 3)
    return 4;
  std::printf("updates=%u, x=%g\n", state.updates, static_cast<double>(final.translation[0]));
  return app->shutdown().failed() ? 5 : 0;
}
