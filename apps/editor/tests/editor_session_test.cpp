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
      session.selection().is_valid()) {
    return 5;
  }
  return 0;
} catch (...) {
  return 99;
}
