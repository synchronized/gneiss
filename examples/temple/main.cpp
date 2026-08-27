// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/input.hpp>
#include <gneiss/scene.h>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace {

struct example_state {
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_node_id rune_node = GNEISS_NULL_SCENE_NODE_ID;
  gneiss_action rotate = GNEISS_NULL_ACTION;
  gneiss_action quit = GNEISS_NULL_ACTION;
  double angle = 0.0;
};

gneiss_result update_temple(gneiss_application application, const gneiss_frame_time* time,
                            void* user_data) {
  if (time == nullptr || user_data == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  auto* state = static_cast<example_state*>(user_data);
  gneiss_action_state rotate = GNEISS_ACTION_STATE_INIT;
  gneiss_action_state quit = GNEISS_ACTION_STATE_INIT;
  if (gneiss_application_get_action_state(application, state->rotate, &rotate) != GNEISS_SUCCESS ||
      gneiss_application_get_action_state(application, state->quit, &quit) != GNEISS_SUCCESS) {
    return GNEISS_ERROR_INVALID_STATE;
  }
  if (quit.pressed != 0U) {
    return gneiss_application_request_exit(application);
  }
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  state->angle += static_cast<double>(time->delta_ns) / nanoseconds_per_second * rotate.value;
  const auto half_angle = state->angle * 0.5;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.translation[2] = 0.1F;
  transform.rotation[2] = static_cast<float>(std::sin(half_angle));
  transform.rotation[3] = static_cast<float>(std::cos(half_angle));
  transform.scale[0] = 0.9F;
  transform.scale[1] = 0.9F;
  return gneiss_scene_node_set_local_transform(state->world, state->rune_node, &transform);
}

} // namespace

int main(int argc, char** argv) {
  const bool smoke = argc == 2 && std::string_view{argv[1]} == "--smoke";
  example_state state;
  constexpr std::string_view title = "Gneiss Stone Temple";
  constexpr std::string_view asset_root = "assets";
  constexpr std::string_view scene_uri = "asset://scenes/temple.scene.json";
  constexpr std::string_view input_map_uri = "asset://input/default.input-map.json";
  constexpr std::string_view rune_uuid = "10000000-0000-4000-8000-000000000008";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_temple;
  desc.platform = GNEISS_APPLICATION_PLATFORM_GRANIT;
  desc.window_title = title.data();
  desc.window_title_length = static_cast<std::uint32_t>(title.size());
  desc.asset_root = asset_root.data();
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
      gneiss::find_action(application.get(), "move_horizontal", state.rotate) !=
          gneiss::result::success ||
      gneiss::find_action(application.get(), "quit", state.quit) != gneiss::result::success ||
      gneiss_scene_instance_find_node(application.get(), scene, rune_uuid.data(), rune_uuid.size(),
                                      &state.rune_node) != GNEISS_SUCCESS) {
    return 2;
  }
  if (application.run(smoke ? 3U : 0U) != gneiss::result::success) {
    return 3;
  }
  return gneiss_scene_instance_unload(application.get(), scene) == GNEISS_SUCCESS ? 0 : 4;
}
