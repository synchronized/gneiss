// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/error.hpp>
#include <gneiss/engine/function/scene/game_object.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <cstdio>
int main() try {
  gneiss::install_fatal_handler();
  gneiss::game_object moving;
  unsigned updates{};
  std::shared_ptr<gneiss::runtime::application> app;
  gneiss::runtime::application_configuration desc;
  desc.callbacks.update = [&](const auto&) {
    auto transform = moving.local_transform();
    if (!transform)
      return transform.error();
    transform->translation.x += 1;
    auto status = moving.set_local_transform(*transform);
    if (++updates == 3)
      app->request_exit();
    return status;
  };
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
  moving = *object;
  gneiss::math::transform initial;
  initial.translation = {1, 2, 3};
  if (object->set_local_transform(initial).failed() || app->run(8).failed())
    return 3;
  auto final = object->local_transform();
  if (!final || updates != 3 || final->translation[0] != 4 || final->translation[1] != 2 ||
      final->translation[2] != 3)
    return 4;
  std::printf("updates=%u, x=%g\n", updates, static_cast<double>(final->translation[0]));
  return app->shutdown().failed() ? 5 : 0;
} catch (...) {
  gneiss::fatal_current_exception();
}
