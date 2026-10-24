// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/c_owner.hpp"
#include "commands/runtime_author_apply.hpp"
#include "engine/core/result.hpp"
#include "project_workspace.hpp"
#include "runtime/application_registry.hpp"
#include "runtime_launch.hpp"

#include "engine/values/core/result.hpp"
#include "engine/values/log.hpp"
#include "engine/values/render.hpp"
#include "engine/values/scene.hpp"
#include <gneiss/engine/application.h>

#include <chrono>
#include <filesystem>
#include <string>

using namespace gneiss::host;

namespace {

[[nodiscard]] gneiss::engine::result
create_application(const std::filesystem::path& asset_root,
                   gneiss::test_support::application_owner& output) {
  const auto root = asset_root.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  return gneiss::engine::result{gneiss_application_create(&desc, output.put())};
}

} // namespace

int main() try {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-runtime-launch-test-" + std::to_string(suffix));
  const auto project_root = root / "project";
  gneiss::host::editor_project project;
  if (gneiss::host::create_editor_project(project_root, "Runtime Launch", project) !=
      gneiss::engine::result::success) {
    return 1;
  }

  gneiss::test_support::application_owner author_application;
  gneiss_world author_world = GNEISS_NULL_WORLD;
  gneiss::editor::scene_document author_session;
  if (create_application(project.asset_root, author_application) !=
          gneiss::engine::result::success ||
      gneiss::engine::result{gneiss_application_get_world(
          author_application.get(), &author_world)} != gneiss::engine::result::success ||
      author_session.open(*gneiss::runtime::find_application(author_application.get()),
                          author_world, project.startup_scene) != gneiss::engine::result::success) {
    return 2;
  }

  gneiss::editor::runtime_launch_request request;
  if (gneiss::editor::inspect_runtime_launch(author_session, project_root, request) !=
          gneiss::editor::runtime_launch_state::ready ||
      request.project_root != project_root) {
    return 3;
  }

  gneiss::scene_node_id author_node;
  if (author_session.create_node("Author Node", {}, author_node) !=
          gneiss::engine::result::success ||
      !author_session.is_dirty() ||
      gneiss::editor::inspect_runtime_launch(author_session, project_root, request) !=
          gneiss::editor::runtime_launch_state::requires_save ||
      !request.project_root.empty()) {
    return 4;
  }
  if (gneiss::editor::save_and_prepare_runtime_launch(author_session, project.asset_root,
                                                      project_root,
                                                      request) != gneiss::engine::result::success ||
      author_session.is_dirty() || request.project_root != project_root) {
    return 5;
  }
  const auto author_node_count = author_session.nodes().size();

  const auto* created_author = author_session.find_node(author_session.selected_node()->uuid);
  if (created_author == nullptr) {
    return 6;
  }
  const std::string author_uuid = created_author->uuid;
  gneiss::editor::runtime_author_transform runtime_snapshot;
  runtime_snapshot.uuid = author_uuid;
  runtime_snapshot.local_transform = created_author->local_transform;
  runtime_snapshot.local_transform.translation[0] = 4.0F;
  gneiss::editor::editor_command_history history;
  if (gneiss::editor::apply_runtime_transform_to_author(
          author_session, history, runtime_snapshot) != gneiss::engine::result::success ||
      !history.is_dirty() || !author_session.is_dirty() ||
      author_session.find_node(runtime_snapshot.uuid)->local_transform.translation[0] != 4.0F ||
      author_session.save(project.asset_root) != gneiss::engine::result::success) {
    return 6;
  }
  history.mark_saved();
  if (author_session.is_dirty() || history.is_dirty() ||
      history.undo() != gneiss::engine::result::success || !history.is_dirty() ||
      author_session.find_node(runtime_snapshot.uuid)->local_transform.translation[0] == 4.0F ||
      history.redo() != gneiss::engine::result::success || history.is_dirty() ||
      author_session.find_node(runtime_snapshot.uuid)->local_transform.translation[0] != 4.0F) {
    return 6;
  }
  author_session.clear_dirty();
  gneiss::editor::runtime_author_transform unmapped;
  unmapped.uuid = "runtime-only";
  if (gneiss::editor::apply_runtime_transform_to_author(author_session, history, unmapped) !=
      gneiss::engine::result::not_found) {
    return 6;
  }

  gneiss::test_support::application_owner runtime_application;
  gneiss_world runtime_world = GNEISS_NULL_WORLD;
  gneiss::editor::scene_document runtime_session;
  if (create_application(project.asset_root, runtime_application) !=
          gneiss::engine::result::success ||
      gneiss::engine::result{gneiss_application_get_world(
          runtime_application.get(), &runtime_world)} != gneiss::engine::result::success ||
      runtime_session.open(*gneiss::runtime::find_application(runtime_application.get()),
                           runtime_world,
                           project.startup_scene) != gneiss::engine::result::success) {
    return 6;
  }
  gneiss::scene_node_id runtime_node;
  if (runtime_session.create_node("Runtime Only", {}, runtime_node) !=
          gneiss::engine::result::success ||
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
