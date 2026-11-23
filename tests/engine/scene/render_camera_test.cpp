// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include <gneiss/engine/function/meta_registration.hpp>
#include <gneiss/engine/function/scene/camera_selection.hpp>
#include <gneiss/engine/function/scene/components.hpp>
#include <gneiss/engine/function/scene/rendering/render_camera.hpp>
#include <gneiss/engine/function/scene/world.hpp>
#include <iostream>
#include <thread>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      std::cerr << __LINE__ << ": " #x "\n";                                                       \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)
int main() {
  using namespace gneiss;
  CHECK(initialize_meta().ok());
  auto world = create_native();
  CHECK(world);
  auto& owner = **world;
  auto scene = owner.create_scene();
  CHECK(scene);
  auto object = scene->create_object();
  CHECK(object);
  auto camera = camera_component{{1.04719755F, 0.1F, 1000.F}, false};
  CHECK(camera.settings.valid() && object->add_component<camera_component>(camera).ok());
  CHECK(set_active_camera(owner, object->entity()).ok());
  auto selected = select_render_camera(owner, 1280, 720);
  CHECK(selected && *selected && (**selected).viewport_width == 1280 &&
        (**selected).viewport_height == 720 && (**selected).view[3][3] == 1.F &&
        (**selected).projection[0][0] != 0.F);
  view_camera override_camera;
  override_camera.transform.translation.x = 17;
  selected = select_render_camera(owner, 800, 600, &override_camera);
  entity_id active;
  CHECK(get_active_camera(owner, active).ok() && active == object->entity());
  CHECK(selected && *selected && (**selected).transform.translation.x == 17);
  auto invalid = override_camera;
  invalid.camera.near_plane = -1;
  CHECK(select_render_camera(owner, 800, 600, &invalid).error() == error::invalid_argument);
  CHECK(select_render_camera(owner, 0, 600).error() == error::invalid_argument);
  error query_error;
  std::thread worker([&] { query_error = select_render_camera(owner, 800, 600).error(); });
  worker.join();
  CHECK(query_error == error::invalid_state);
  object->destroy();
  selected = select_render_camera(owner, 800, 600);
  CHECK(selected && !*selected);
  selected = select_render_camera(owner, 640, 480, &override_camera);
  CHECK(selected && *selected && (**selected).viewport_width == 640 &&
        (**selected).transform.translation.x == 17);
  return 0;
}
