// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/engine/application.hpp>
#include <gneiss/engine/input.hpp>
#include <gneiss/engine/scene.h>

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
  gneiss::action_id orbit{};
  gneiss::action_id quit{};
  double angle = 0.0;
};

gneiss_result update_temple(gneiss_application application, const gneiss_frame_time* time,
                            void* user_data) {
  if (time == nullptr || user_data == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto* state = static_cast<example_state*>(user_data);
  gneiss::action_state orbit{};
  gneiss::action_state quit{};
  if (gneiss::get_action_state(application, state->orbit, orbit).failed() ||
      gneiss::get_action_state(application, state->quit, quit).failed()) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (quit.pressed) {
    return gneiss_application_request_exit(application);
  }
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  state->angle += static_cast<double>(time->delta_ns) / nanoseconds_per_second * orbit.value;
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
  return gneiss_scene_node_set_local_transform(state->world, state->camera_node, &transform);
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
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_temple;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.asset_root = asset_root.c_str();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());

  gneiss::application application;
  if (gneiss::application::create(desc, application) != gneiss::result::success ||
      application.get_world(state.world) != gneiss::result::success) {
    return 1;
  }
  gneiss_scene_instance scene = GNEISS_NULL_SCENE_INSTANCE;
  if (gneiss_scene_instance_load(application.get(), scene_uri.data(), scene_uri.size(), &scene) !=
          GNEISS_SUCCESS ||
      gneiss::load_action_map(application.get(), input_map_uri) != gneiss::result::success ||
      application.find_action("move_horizontal", state.orbit) != gneiss::result::success ||
      application.find_action("quit", state.quit) != gneiss::result::success ||
      gneiss_scene_instance_find_node(application.get(), scene, camera_uuid.data(),
                                      camera_uuid.size(), &state.camera_node) != GNEISS_SUCCESS) {
    return 2;
  }
  if (application.run(smoke ? 3U : 0U) != gneiss::result::success) {
    return 3;
  }
  return gneiss_scene_instance_unload(application.get(), scene) == GNEISS_SUCCESS ? 0 : 4;
}

int main(int argc, char** argv) {
  try {
    return run_example(argc, argv);
  } catch (...) {
    return 99;
  }
}
