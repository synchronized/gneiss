// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_browser_model.h"
#include "asset_import_controller.h"
#include "editor_command_history.h"
#include "editor_project.h"
#include "editor_session.h"

#include <gneiss/application.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

struct temporary_project final {
  std::filesystem::path root;
  ~temporary_project() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};

[[nodiscard]] bool is_imported_mesh(const gneiss::editor::asset_browser_entry& entry) {
  return entry.kind == gneiss::editor::asset_browser_kind::imported_output &&
         entry.asset_uri.ends_with(".gneiss-mesh");
}

[[nodiscard]] bool is_imported_material(const gneiss::editor::asset_browser_entry& entry) {
  return entry.kind == gneiss::editor::asset_browser_kind::imported_output &&
         entry.asset_uri.ends_with(".material.json");
}

[[nodiscard]] gneiss::result create_application(const std::filesystem::path& asset_root,
                                                gneiss::application& output) {
  const auto root = asset_root.generic_string();
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  desc.asset_root = root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(root.size());
  return gneiss::application::create(desc, output);
}

} // namespace

int main() try { // NOLINT(bugprone-exception-escape): 测试入口统一返回失败码。
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-lantern-workflow-" + std::to_string(suffix));
  const temporary_project cleanup{root};
  std::filesystem::copy(GNEISS_EDITOR_LANTERN_PROJECT, root,
                        std::filesystem::copy_options::recursive);

  gneiss::editor::editor_project project;
  if (gneiss::editor::load_editor_project(root, project) != gneiss::result::success) {
    return 1;
  }
  const auto imported = gneiss::editor::import_external_asset(
      project.project_root, project.asset_root, GNEISS_EDITOR_LANTERN_SOURCE);
  if (imported.result != gneiss::editor::editor_import_result::success) {
    return 2;
  }

  gneiss::editor::asset_browser_model browser;
  if (browser.refresh(project.project_root, project.asset_root) !=
      gneiss::editor::asset_browser_result::success) {
    return 3;
  }
  const auto source = std::ranges::find_if(browser.entries(), [](const auto& entry) {
    return entry.kind == gneiss::editor::asset_browser_kind::source;
  });
  if (source == browser.entries().end() ||
      source->status != gneiss::editor::asset_browser_status::ready) {
    return 4;
  }

  {
    std::ofstream changed(imported.source_path, std::ios::binary | std::ios::app);
    changed.put('\0');
  }
  if (browser.refresh(project.project_root, project.asset_root) !=
          gneiss::editor::asset_browser_result::success ||
      std::ranges::none_of(browser.entries(), [](const auto& entry) {
        return entry.kind == gneiss::editor::asset_browser_kind::source &&
               entry.status == gneiss::editor::asset_browser_status::stale;
      })) {
    return 5;
  }
  std::filesystem::copy_file(GNEISS_EDITOR_LANTERN_SOURCE, imported.source_path,
                             std::filesystem::copy_options::overwrite_existing);
  if (gneiss::editor::reimport_source_asset(project.project_root, project.asset_root,
                                            imported.source_path)
              .result != gneiss::editor::editor_import_result::success ||
      browser.refresh(project.project_root, project.asset_root) !=
          gneiss::editor::asset_browser_result::success) {
    return 6;
  }

  const auto mesh = std::ranges::find_if(browser.entries(), is_imported_mesh);
  const auto material = std::ranges::find_if(browser.entries(), is_imported_material);
  if (mesh == browser.entries().end() || material == browser.entries().end()) {
    return 7;
  }

  gneiss::application application;
  gneiss_world world = GNEISS_NULL_WORLD;
  gneiss::editor::editor_session session;
  if (create_application(project.asset_root, application) != gneiss::result::success ||
      application.get_world(world) != gneiss::result::success ||
      session.open(application.get(), world, project.startup_scene) != gneiss::result::success) {
    return 8;
  }
  gneiss::scene_node_id node;
  if (session.create_mesh_renderer_node("Imported Lantern", mesh->asset_uri, material->asset_uri,
                                        node) != gneiss::result::success) {
    return 9;
  }
  const auto* created = session.selected_node();
  const gneiss::editor::scene_node_snapshot snapshot{.uuid = created->uuid,
                                                     .parent_uuid = {},
                                                     .display_name = created->display_name,
                                                     .mesh_uri = created->mesh_uri,
                                                     .material_uri = created->material_uri};
  gneiss::editor::editor_command_history history;
  if (history.record({.label = "创建导入节点",
                      .undo =
                          [&session, uuid = snapshot.uuid] {
                            const auto* current = session.find_node(uuid);
                            if (current == nullptr) {
                              return gneiss::result::not_found;
                            }
                            gneiss::editor::scene_node_snapshot discarded;
                            return session.destroy_node(current->node, discarded);
                          },
                      .redo =
                          [&session, snapshot] {
                            gneiss::scene_node_id restored;
                            return session.restore_mesh_renderer_node(snapshot, restored);
                          }}) != gneiss::result::success ||
      history.undo() != gneiss::result::success || session.find_node(snapshot.uuid) != nullptr ||
      history.redo() != gneiss::result::success || session.find_node(snapshot.uuid) == nullptr ||
      session.save(project.asset_root) != gneiss::result::success) {
    return 10;
  }
  session.close();

  gneiss::editor::editor_session reloaded;
  if (reloaded.open(application.get(), world, project.startup_scene) != gneiss::result::success ||
      reloaded.find_node(snapshot.uuid) == nullptr) {
    return 11;
  }
  reloaded.close();
  application.reset();
  return 0;
} catch (...) {
  return 99;
}
