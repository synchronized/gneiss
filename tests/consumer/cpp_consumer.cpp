// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/application.hpp>
#include <gneiss/scene.hpp>

#include <cstdint>
#include <string>
#include <string_view>

int main() {
  constexpr std::string_view asset_root = GNEISS_CONSUMER_ASSET_ROOT;
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";

  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());

  gneiss::application application;
  if (gneiss::application::create(desc, application) != gneiss::result::success) {
    return 1;
  }

  gneiss::scene_instance scene;
  if (gneiss::scene_instance::load(application.get(), scene_uri, scene) !=
      gneiss::result::success) {
    return 2;
  }
  std::uint64_t node_count = 0;
  gneiss::scene_instance_node_info node_info = GNEISS_SCENE_INSTANCE_NODE_INFO_INIT;
  if (scene.get_node_count(node_count) != gneiss::result::success || node_count == 0U ||
      scene.get_node_info(0U, node_info) != gneiss::result::success ||
      node_info.node == GNEISS_NULL_SCENE_NODE_ID) {
    return 3;
  }
  std::string json;
  if (scene.serialize(json) != gneiss::result::success || json.empty()) {
    return 4;
  }
  scene.reset();
  return 0;
}
