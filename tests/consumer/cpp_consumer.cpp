// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors
#include "../../examples/support/scope_exit.hpp"
#include <gneiss/gneiss.h>
#include <string>
#include <string_view>
int main() {
  constexpr std::string_view root = GNEISS_CONSUMER_ASSET_ROOT;
  constexpr std::string_view uri = "asset://scenes/triangle.scene.json";
  gneiss_application app{};
  example::scope_exit close{[&] {
    if (app)
      (void)gneiss_application_destroy(app);
  }};
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<uint32_t>(root.size());
  if (gneiss_application_create(&desc, &app) != GNEISS_SUCCESS)
    return 1;
  constexpr std::string_view input_uri = "asset://input/default.input-map.json";
  gneiss_action action{};
  gneiss_action_state input = GNEISS_ACTION_STATE_INIT;
  if (gneiss_application_load_action_map(app, input_uri.data(), input_uri.size()) !=
          GNEISS_SUCCESS ||
      gneiss_application_find_action(app, "move_horizontal", 15, &action) != GNEISS_SUCCESS ||
      gneiss_application_get_action_state(app, action, &input) != GNEISS_SUCCESS)
    return 6;
  gneiss_scene_instance scene{};
  example::scope_exit unload{[&] {
    if (scene)
      (void)gneiss_scene_instance_unload(app, scene);
  }};
  if (gneiss_scene_instance_load(app, uri.data(), uri.size(), &scene) != GNEISS_SUCCESS)
    return 2;
  uint64_t count{};
  gneiss_scene_instance_node_info info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  if (gneiss_scene_instance_get_node_count(app, scene, &count) != GNEISS_SUCCESS || !count ||
      gneiss_scene_instance_get_node_info(app, scene, 0, &info) != GNEISS_SUCCESS || !info.node)
    return 3;
  uint64_t length{};
  if (gneiss_scene_instance_serialize(app, scene, nullptr, 0, &length) != GNEISS_SUCCESS || !length)
    return 4;
  std::string json(static_cast<size_t>(length), '\0');
  if (gneiss_scene_instance_serialize(app, scene, json.data(), json.size(), &length) !=
      GNEISS_SUCCESS)
    return 4;
  if (gneiss_scene_instance_unload(app, scene) != GNEISS_SUCCESS)
    return 5;
  scene = 0;
  constexpr std::string_view prefab_uri = "asset://scenes/prefab.scene.json";
  if (gneiss_scene_instance_load(app, prefab_uri.data(), prefab_uri.size(), &scene) !=
      GNEISS_SUCCESS)
    return 5;
  gneiss_scene_prefab_node_info prefab = GNEISS_SCENE_PREFAB_NODE_INFO_INIT;
  gneiss_scene_node_id new_root{};
  gneiss_scene_prefab_refresh_token refresh{};
  if (gneiss_scene_instance_get_prefab_node_info(app, scene, 0, &prefab) != GNEISS_SUCCESS ||
      gneiss_scene_instance_refresh_prefab_instance(app, scene, prefab.node, &new_root, &refresh) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_toggle_prefab_refresh(app, scene, refresh, &new_root) !=
          GNEISS_SUCCESS ||
      gneiss_scene_instance_release_prefab_refresh(app, scene, refresh) != GNEISS_SUCCESS)
    return 5;
  return 0;
}
