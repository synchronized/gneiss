// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_session.h"
#include "runtime_author_apply.h"

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
  auto source_moved = session.selected_prefab_node()->local_transform;
  source_moved.translation[1] = 5.0F;
  if (session.set_local_transform(session.selection(), source_moved) != gneiss::result::success ||
      session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->local_transform.translation[1] != 5.0F ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN) == 0U) {
    return 7;
  }
  gneiss::transform previous_source{};
  if (session.restore_prefab_transform_field(session.selection(),
                                             GNEISS_TRANSFORM_FIELD_TRANSLATION,
                                             previous_source) != gneiss::result::success ||
      previous_source.translation[1] != 5.0F || session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->local_transform.translation[0] != 1.0F ||
      session.selected_prefab_node()->local_transform.translation[1] != 0.0F ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN) != 0U ||
      session.set_local_transform(session.selection(), previous_source) !=
          gneiss::result::success ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_TRANSLATION_OVERRIDDEN) == 0U ||
      session.restore_prefab_transform(session.selection(), previous_source) !=
          gneiss::result::success ||
      session.selected_prefab_node()->override_flags != 0U) {
    return 8;
  }
  const auto* prefab_source = session.selected_prefab_node();
  gneiss::ipc_inspection_node runtime_source;
  runtime_source.uuid = prefab_source->source_node_uuid;
  runtime_source.prefab_instance_uuid = prefab_source->instance_uuid;
  runtime_source.prefab_source_node_uuid = prefab_source->source_node_uuid;
  runtime_source.local_transform = prefab_source->local_transform;
  runtime_source.local_transform.scale[0] = 2.0F;
  gneiss::editor::editor_command_history history;
  if (gneiss::editor::apply_runtime_transform_to_author(session, history, runtime_source) !=
          gneiss::result::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F ||
      (session.selected_prefab_node()->override_flags &
       GNEISS_SCENE_PREFAB_NODE_SCALE_OVERRIDDEN) == 0U ||
      history.undo() != gneiss::result::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 1.0F ||
      history.redo() != gneiss::result::success ||
      session.selected_prefab_node()->local_transform.scale[0] != 2.0F) {
    return 9;
  }
  auto invalid_runtime_source = runtime_source;
  invalid_runtime_source.prefab_instance_uuid.clear();
  auto stale_runtime_source = runtime_source;
  stale_runtime_source.prefab_instance_uuid = "ffffffff-ffff-4fff-8fff-ffffffffffff";
  if (gneiss::editor::apply_runtime_transform_to_author(session, history, invalid_runtime_source) !=
          gneiss::result::invalid_argument ||
      gneiss::editor::apply_runtime_transform_to_author(session, history, stale_runtime_source) !=
          gneiss::result::not_found) {
    return 9;
  }
  gneiss::scene_node_id prefab_root;
  if (session.create_prefab_instance("Second Lamp", prefab_uri, session.nodes()[0].node,
                                     prefab_root) != gneiss::result::success ||
      !session.is_dirty() || session.prefab_nodes().size() != 4U ||
      session.selected_prefab_node() == nullptr ||
      !session.selected_prefab_node()->is_instance_root ||
      session.selected_prefab_node()->node != prefab_root) {
    return 10;
  }
  gneiss::editor::prefab_instance_snapshot snapshot;
  if (session.destroy_prefab_instance(prefab_root, snapshot) != gneiss::result::success ||
      session.prefab_nodes().size() != 2U || session.selection().is_valid() ||
      session.restore_prefab_instance(snapshot, prefab_root) != gneiss::result::success ||
      session.prefab_nodes().size() != 4U ||
      session.rename_prefab_instance(prefab_root, "Restored Lamp") != gneiss::result::success) {
    return 11;
  }
  auto moved = session.selected_prefab_node()->local_transform;
  moved.translation[1] = 3.0F;
  const auto stale_root = prefab_root;
  gneiss_scene_prefab_refresh_token refresh_token = GNEISS_NULL_SCENE_PREFAB_REFRESH_TOKEN;
  if (session.set_local_transform(prefab_root, moved) != gneiss::result::success ||
      session.refresh_prefab_instance(prefab_root, prefab_root, refresh_token) !=
          gneiss::result::success ||
      prefab_root == stale_root || session.selected_prefab_node() == nullptr ||
      session.selected_prefab_node()->display_name != "Restored Lamp" ||
      session.selected_prefab_node()->local_transform.translation[1] != 3.0F ||
      session.toggle_prefab_refresh(refresh_token, prefab_root) != gneiss::result::success ||
      session.toggle_prefab_refresh(refresh_token, prefab_root) != gneiss::result::success) {
    return 12;
  }
  session.release_prefab_refresh(refresh_token);
  return 0;
} catch (...) {
  return 99;
}
