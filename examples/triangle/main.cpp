// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/scene.h>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace {

struct example_state {
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss_scene_node_id triangle_node = GNEISS_NULL_SCENE_NODE_ID;
};

gneiss_result update_triangle(gneiss_application /*application*/, const gneiss_frame_time* time,
                              void* user_data) {
  if (time == nullptr || user_data == nullptr) {
    return GNEISS_ERROR_INVALID_ARGUMENT;
  }
  const auto* state = static_cast<const example_state*>(user_data);
  constexpr double nanoseconds_per_second = 1'000'000'000.0;
  const auto half_angle = static_cast<double>(time->elapsed_ns) / nanoseconds_per_second * 0.5;
  gneiss_transform transform = GNEISS_TRANSFORM_IDENTITY;
  transform.rotation[2] = static_cast<float>(std::sin(half_angle));
  transform.rotation[3] = static_cast<float>(std::cos(half_angle));
  return gneiss_scene_node_set_local_transform(state->world, state->triangle_node, &transform);
}

} // namespace

int main() {
  example_state state;
  constexpr std::string_view title = "Gneiss Triangle";
  constexpr std::string_view asset_root = "assets";
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view triangle_uuid = "00000000-0000-4000-8000-000000000003";
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.user_data = &state;
  desc.update = update_triangle;
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
      gneiss_scene_instance_find_node(application.get(), scene, triangle_uuid.data(),
                                      triangle_uuid.size(),
                                      &state.triangle_node) != GNEISS_SUCCESS) {
    return 2;
  }
  if (application.run() != gneiss::result::success) {
    return 3;
  }
  return gneiss_scene_instance_unload(application.get(), scene) == GNEISS_SUCCESS ? 0 : 4;
}
