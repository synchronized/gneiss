// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <cstdio>
#include <gneiss/engine/core/base/error.hpp>
#include <gneiss/engine/function/application/application.hpp>
#include <gneiss/engine/function/scene/game_object.hpp>
#include <gneiss/engine/function/scene/world.hpp>
int main() try {
  gneiss::install_fatal_handler();
  auto created = gneiss::runtime::application::create({});
  if (!created)
    return 1;
  auto created_world = gneiss::create_native();
  if (!created_world)
    return 2;
  auto world = std::move(*created_world);
  auto object_scene = world->create_scene();
  if (!object_scene)
    return 2;
  auto object = object_scene->create_object({"example", {}});
  if (!object)
    return 2;
  gneiss::math::transform initial;
  initial.translation = {1, 2, 3};
  if (object->set_local_transform(initial).failed())
    return 3;
  unsigned updates{};
  gneiss::math::transform final;
  auto* app = created->get();
  auto owner = std::move(*created);
  app->add_system(gneiss::app::stage::update, "move", [&](const auto&) {
    auto transform = object->local_transform();
    transform.translation.x += 1;
    auto status = object->set_local_transform(transform);
    if (++updates == 3)
      app->request_exit();
    return status;
  });
  app->add_system(gneiss::app::stage::synchronize, "world_changes",
                  [&] { world->flush_changes(); });
  app->on_stop("release_world", [&] {
    final = object->local_transform();
    object_scene->unload();
    world.reset();
  });
  if (!owner->run())
    return 3;
  if (updates != 3 || final.translation[0] != 4 || final.translation[1] != 2 ||
      final.translation[2] != 3)
    return 4;
  std::printf("updates=%u, x=%g\n", updates, static_cast<double>(final.translation[0]));
  return 0;
} catch (...) {
  gneiss::fatal_current_exception();
}
