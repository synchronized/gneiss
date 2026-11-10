// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "engine/core/fatal.hpp"
#include "engine/function/scene/world.hpp"
#include "runtime/application.hpp"
#include "scene_update.hpp"
#include <cstdio>
int main() try {
  gneiss::engine::install_fatal_handler();
  example::scene_state state;
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  desc.callbacks.update = [&](const auto&) { return example::update(*app, state); };
  auto created = gneiss::runtime::application::create(desc);
  if (!created)
    return 1;
  app = std::move(*created);
  auto* world = app->native_world();
  auto object_scene = world->create_scene();
  if (!object_scene)
    return 2;
  auto object = object_scene->create_object({"example", {}});
  if (!object)
    return 2;
  state.entity = object->entity();
  gneiss::engine::math::transform initial;
  initial.translation = {1, 2, 3};
  if (object->set_local_transform(initial).failed() || app->run(8).failed())
    return 3;
  auto final = object->local_transform();
  if (!final || state.updates != 3 || final->translation[0] != 4 || final->translation[1] != 2 ||
      final->translation[2] != 3)
    return 4;
  std::printf("updates=%u, x=%g\n", state.updates, static_cast<double>(final->translation[0]));
  return app->shutdown().failed() ? 5 : 0;
} catch (...) {
  gneiss::engine::fatal_current_exception();
}
