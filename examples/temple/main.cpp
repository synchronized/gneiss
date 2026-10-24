// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scope_exit.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"
#include "runtime/application_bridge.hpp"
#include <gneiss/engine/application.h>
#include <gneiss/engine/scene.h>
#include <gneiss/engine/world.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct example_state {
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_node_id camera_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss::engine::action_id orbit{};
  gneiss::engine::action_id quit{};
  double angle = 0.0;
};

gneiss::engine::result update_temple(gneiss::runtime::application& application,
                                     const gneiss::engine::frame_time& time, void* user_data) {
  if (user_data == nullptr) {
    return gneiss::engine::result::invalid_argument;
  }
  auto* state = static_cast<example_state*>(user_data);
  gneiss::engine::action_state orbit{};
  gneiss::engine::action_state quit{};
  if (application.get_action_state(state->orbit, orbit).failed() ||
      application.get_action_state(state->quit, quit).failed()) {
    return gneiss::engine::result::invalid_state;
  }
  if (quit.pressed) {
    application.request_exit();
    return gneiss::engine::result::success;
  }
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  state->angle += static_cast<double>(time.delta_ns) / nanoseconds_per_second * orbit.value;
  constexpr double radius = 6.0;
  constexpr double height = 2.3;
  const auto yaw_half = state->angle * 0.5;
  const auto pitch_half = std::atan2(-height, radius) * 0.5;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[0] = static_cast<float>(std::sin(state->angle) * radius);
  transform.translation[1] = static_cast<float>(height);
  transform.translation[2] = static_cast<float>(std::cos(state->angle) * radius);
  transform.rotation[0] = static_cast<float>(std::cos(yaw_half) * std::sin(pitch_half));
  transform.rotation[1] = static_cast<float>(std::sin(yaw_half) * std::cos(pitch_half));
  transform.rotation[2] = static_cast<float>(-std::sin(yaw_half) * std::sin(pitch_half));
  transform.rotation[3] = static_cast<float>(std::cos(yaw_half) * std::cos(pitch_half));
  return gneiss::engine::result{
      gneiss_scene_node_set_local_transform(state->world, state->camera_node, &transform)};
}

} // namespace

int run_example(int argc, char** argv) {
  const bool smoke = argc == 2 && std::string_view{argv[1]} == "--smoke";
  example_state state;
  constexpr std::string_view title = "Gneiss Stone Temple";
  constexpr std::string_view scene_uri = "asset://scenes/temple.scene.json";
  constexpr std::string_view input_map_uri = "asset://input/default.input-map.json";
  constexpr std::string_view camera_uuid = "10000000-0000-4000-8000-000000000002";
  std::error_code path_error;
  const auto executable = std::filesystem::absolute(argv[0], path_error);
  const auto installed_asset_root =
      executable.parent_path().parent_path() / "share/gneiss/examples/temple/assets";
  const auto has_installed_assets =
      !path_error && std::filesystem::is_directory(installed_asset_root, path_error);
  const auto asset_root = has_installed_assets && !path_error
                              ? installed_asset_root.string()
                              : std::string{GNEISS_TEMPLE_SOURCE_ASSET_ROOT};
  std::shared_ptr<gneiss::runtime::application> native_application;
  gneiss::runtime::application_bridge bridge;
  gneiss::runtime::application_configuration config;
  config.use_granit_window = true;
  config.window.title = title;
  config.asset_root = asset_root;
  config.callbacks.update = [&](auto, const auto& time) {
    return update_temple(*native_application, time, &state);
  };
  example::scope_exit close{[&] {
    if (native_application && native_application->accepts_requests())
      (void)native_application->shutdown(bridge.get());
  }};
  auto create_result = gneiss::runtime::application::create(config, native_application);
  if (create_result.ok())
    create_result = bridge.bind(native_application);
  if (create_result.failed()) {
    return 1;
  }
  const auto application = bridge.get();
  state.world = native_application->world();
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  if (gneiss_scene_instance_load(application, scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS ||
      native_application->load_action_map(input_map_uri) != gneiss::engine::result::success ||
      native_application->find_action("move_horizontal", state.orbit) !=
          gneiss::engine::result::success ||
      native_application->find_action("quit", state.quit) != gneiss::engine::result::success ||
      gneiss_scene_instance_find_node(application, scene, camera_uuid.data(), camera_uuid.size(),
                                      &state.camera_node) != GNEISS_SUCCESS) {
    return 2;
  }
  if (native_application->run(application, smoke ? 3U : 0U) != gneiss::engine::result::success) {
    return 3;
  }
  return gneiss_scene_instance_unload(application, scene) == GNEISS_SUCCESS ? 0 : 4;
}

int main(int argc, char** argv) {
  try {
    return run_example(argc, argv);
  } catch (...) {
    return 99;
  }
}
