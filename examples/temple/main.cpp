// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../support/scope_exit.hpp"
#include "engine/core/result.hpp"
#include "engine/core/tasks/task_scheduler.hpp"
#include "engine/function/scene/scene_instances.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct example_state {
  gneiss::scene_node_id camera_node{};
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
  gneiss::engine::transform transform{};
  transform.translation[0] = static_cast<float>(std::sin(state->angle) * radius);
  transform.translation[1] = static_cast<float>(height);
  transform.translation[2] = static_cast<float>(std::cos(state->angle) * radius);
  transform.rotation[0] = static_cast<float>(std::cos(yaw_half) * std::sin(pitch_half));
  transform.rotation[1] = static_cast<float>(std::sin(yaw_half) * std::cos(pitch_half));
  transform.rotation[2] = static_cast<float>(-std::sin(yaw_half) * std::sin(pitch_half));
  transform.rotation[3] = static_cast<float>(std::cos(yaw_half) * std::cos(pitch_half));
  return application.native_world()->node_set_local_transform(state->camera_node, transform);
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
  gneiss::engine::task_scheduler scheduler(
      {.workers = 0, .mode = gneiss::engine::execution_mode::cooperative});
  bool can_update = false;
  std::shared_ptr<gneiss::runtime::application> native_application;
  gneiss::runtime::application_configuration config;
  config.use_granit_window = true;
  config.window.title = title;
  config.asset_root = asset_root;
  config.callbacks.update = [&](const auto& time) {
    if (!can_update)
      return gneiss::engine::result::success;
    return update_temple(*native_application, time, &state);
  };
  example::scope_exit close{[&] {
    if (native_application && native_application->accepts_requests())
      (void)native_application->shutdown();
  }};
  auto create_result = gneiss::runtime::application::create(config, native_application);
  if (create_result.failed()) {
    return 1;
  }
  if (native_application->attach_task_executor(scheduler).failed() ||
      native_application->begin_action_map(input_map_uri).failed())
    return 2;
  while (native_application->action_map_status() == gneiss::engine::result::not_ready) {
    (void)scheduler.run_ready();
    if (native_application->run(1).failed())
      return 2;
  }
  if (native_application->action_map_status().failed())
    return 2;
  gneiss::scene_instance_id scene{};
  if (native_application->scenes()->load(scene_uri, &scene) != gneiss::engine::result::success ||
      native_application->action_map_status() != gneiss::engine::result::success ||
      native_application->find_action("move_horizontal", state.orbit) !=
          gneiss::engine::result::success ||
      native_application->find_action("quit", state.quit) != gneiss::engine::result::success ||
      native_application->scenes()->find_node(scene, camera_uuid, state.camera_node) !=
          gneiss::engine::result::success) {
    return 2;
  }
  can_update = true;
  if (native_application->run(smoke ? 3U : 0U) != gneiss::engine::result::success) {
    return 3;
  }
  return native_application->scenes()->unload(scene) == gneiss::engine::result::success ? 0 : 4;
}

int main(int argc, char** argv) {
  try {
    return run_example(argc, argv);
  } catch (...) {
    return 99;
  }
}
