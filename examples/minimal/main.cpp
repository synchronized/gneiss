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
  gneiss::world_owner world;
  gneiss::game_object object;
  unsigned updates{};
  gneiss::math::transform final;
  auto* app = created->get();
  auto owner = std::move(*created);
  app->on_start("create_world", [&]() -> gneiss::result<void> {
    auto created_world = gneiss::create_native();
    if (!created_world)
      return created_world.error();
    world = std::move(*created_world);
    auto scene = world->create_scene();
    if (!scene)
      return scene.error();
    auto created_object = scene->create_object({"example", {}});
    if (!created_object)
      return created_object.error();
    object = *created_object;
    gneiss::math::transform initial;
    initial.translation = {1, 2, 3};
    return object.set_local_transform(initial);
  });
  app->add_system(gneiss::app::stage::update, "move", [&](const auto&) {
    auto transform = object.local_transform();
    transform.translation.x += 1;
    auto status = object.set_local_transform(transform);
    if (++updates == 3)
      app->request_exit();
    return status;
  });
  app->add_system(gneiss::app::stage::synchronize, "world_changes",
                  [&] { world->flush_changes(); });
  app->on_stop("release_world", [&] {
    if (object.is_valid())
      final = object.local_transform();
    object = {};
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
