// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_workspace.h"
#include "runtime_author_apply.h"
#include "runtime_launch.h"

#include <gneiss/application.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

[[nodiscard]] gneiss::result create_application(const std::filesystem::path& asset_root,
                                                gneiss::application& output) {
  const auto root = asset_root.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  return gneiss::application::create(desc, output);
}

} // namespace

int main() try {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-runtime-launch-test-" + std::to_string(suffix));
  const auto project_root = root / "project";
  gneiss::editor::editor_project project;
  if (gneiss::editor::create_editor_project(project_root, "Runtime Launch", project) !=
      gneiss::result::success) {
    return 1;
  }

  gneiss::application author_application;
  gneiss_world author_world = GNEISS_NULL_WORLD;
  gneiss::editor::editor_session author_session;
  if (create_application(project.asset_root, author_application) != gneiss::result::success ||
      author_application.get_world(author_world) != gneiss::result::success ||
      author_session.open(author_application.get(), author_world, project.startup_scene) !=
          gneiss::result::success) {
    return 2;
  }

  gneiss::editor::runtime_launch_request request;
  if (gneiss::editor::inspect_runtime_launch(author_session, project_root, request) !=
          gneiss::editor::runtime_launch_state::ready ||
      request.project_root != project_root) {
    return 3;
  }

  gneiss::scene_node_id author_node;
  if (author_session.create_node("Author Node", {}, author_node) != gneiss::result::success ||
      !author_session.is_dirty() ||
      gneiss::editor::inspect_runtime_launch(author_session, project_root, request) !=
          gneiss::editor::runtime_launch_state::requires_save ||
      !request.project_root.empty()) {
    return 4;
  }
  if (gneiss::editor::save_and_prepare_runtime_launch(
          author_session, project.asset_root, project_root, request) != gneiss::result::success ||
      author_session.is_dirty() || request.project_root != project_root) {
    return 5;
  }
  const auto author_node_count = author_session.nodes().size();

  const auto* created_author = author_session.find_node(author_session.selected_node()->uuid);
  if (created_author == nullptr) {
    return 6;
  }
  gneiss::ipc_inspection_node runtime_snapshot;
  runtime_snapshot.uuid = created_author->uuid;
  runtime_snapshot.local_transform = created_author->local_transform;
  runtime_snapshot.local_transform.translation[0] = 4.0F;
  gneiss::editor::editor_command_history history;
  if (gneiss::editor::apply_runtime_transform_to_author(
          author_session, history, runtime_snapshot) != gneiss::result::success ||
      !history.is_dirty() || !author_session.is_dirty() ||
      author_session.find_node(runtime_snapshot.uuid)->local_transform.translation[0] != 4.0F ||
      author_session.save(project.asset_root) != gneiss::result::success) {
    return 6;
  }
  history.mark_saved();
  if (author_session.is_dirty() || history.is_dirty() ||
      history.undo() != gneiss::result::success || !history.is_dirty() ||
      author_session.find_node(runtime_snapshot.uuid)->local_transform.translation[0] == 4.0F ||
      history.redo() != gneiss::result::success || history.is_dirty() ||
      author_session.find_node(runtime_snapshot.uuid)->local_transform.translation[0] != 4.0F) {
    return 6;
  }
  author_session.clear_dirty();
  gneiss::ipc_inspection_node unmapped;
  unmapped.uuid = "runtime-only";
  if (gneiss::editor::apply_runtime_transform_to_author(author_session, history, unmapped) !=
      gneiss::result::not_found) {
    return 6;
  }

  gneiss::application runtime_application;
  gneiss_world runtime_world = GNEISS_NULL_WORLD;
  gneiss::editor::editor_session runtime_session;
  if (create_application(project.asset_root, runtime_application) != gneiss::result::success ||
      runtime_application.get_world(runtime_world) != gneiss::result::success ||
      runtime_session.open(runtime_application.get(), runtime_world, project.startup_scene) !=
          gneiss::result::success) {
    return 6;
  }
  gneiss::scene_node_id runtime_node;
  if (runtime_session.create_node("Runtime Only", {}, runtime_node) != gneiss::result::success ||
      author_session.nodes().size() != author_node_count || author_session.is_dirty()) {
    return 7;
  }

  runtime_session.close();
  author_session.close();
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
