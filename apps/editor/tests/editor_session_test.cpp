// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_session.h"

#include <gneiss/application.hpp>

#include <string>
#include <string_view>

int main() try {
  constexpr std::string_view scene_uri = "asset://scenes/triangle.scene.json";
  constexpr std::string_view missing_uri = "asset://scenes/missing.scene.json";
  const std::string asset_root = GNEISS_EDITOR_TEST_ASSET_ROOT;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = asset_root.c_str();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss::application application;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::editor::editor_session session;
  if (gneiss::application::create(desc, application) != gneiss::result::success ||
      application.get_world(world) != gneiss::result::success ||
      session.open(application.get(), world, scene_uri) != gneiss::result::success ||
      !session.is_open() || session.is_dirty() || session.uri() != scene_uri ||
      session.nodes().size() != 2U || session.nodes()[0].display_name != "Camera" ||
      session.nodes()[1].display_name != session.nodes()[1].uuid ||
      session.nodes()[1].parent != session.nodes()[0].node) {
    return 1;
  }
  if (session.select(session.nodes()[1].node) != gneiss::result::success ||
      session.selected_node() == nullptr) {
    return 2;
  }
  session.mark_dirty();
  if (!session.is_dirty() ||
      session.open(application.get(), world, missing_uri) != gneiss::result::not_found ||
      !session.is_open() || !session.is_dirty() || session.uri() != scene_uri ||
      session.selected_node() == nullptr) {
    return 3;
  }
  const auto selected = session.selection();
  if (gneiss_scene_node_destroy(world, selected.get()) != GNEISS_SUCCESS ||
      session.validate_selection() != gneiss::result::invalid_handle ||
      session.selection().is_valid()) {
    return 4;
  }
  session.close();
  if (session.is_open() || session.is_dirty() || !session.nodes().empty() ||
      !session.prefab_nodes().empty() || session.selection().is_valid()) {
    return 5;
  }
  constexpr std::string_view prefab_scene_uri = "asset://scenes/prefab.scene.json";
  constexpr std::string_view prefab_uri = "asset://prefabs/test.prefab.json";
  if (session.open(application.get(), world, prefab_scene_uri) != gneiss::result::success ||
      session.nodes().size() != 1U || session.prefab_nodes().size() != 2U ||
      !session.prefab_nodes()[0].is_instance_root || session.prefab_nodes()[0].is_read_only ||
      session.prefab_nodes()[1].is_instance_root || !session.prefab_nodes()[1].is_read_only ||
      session.prefab_nodes()[1].parent != session.prefab_nodes()[0].node) {
    return 6;
  }
  if (session.select(session.prefab_nodes()[1].node) != gneiss::result::success ||
      session.selected_node() != nullptr || session.selected_prefab_node() == nullptr) {
    return 7;
  }
  gneiss::scene_node_id prefab_root;
  if (session.create_prefab_instance("Second Lamp", prefab_uri, session.nodes()[0].node,
                                     prefab_root) != gneiss::result::success ||
      !session.is_dirty() || session.prefab_nodes().size() != 4U ||
      session.selected_prefab_node() == nullptr ||
      !session.selected_prefab_node()->is_instance_root ||
      session.selected_prefab_node()->node != prefab_root) {
    return 8;
  }
  return 0;
} catch (...) {
  return 99;
}
