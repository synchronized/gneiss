// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "../../../../tests/support/async_editor_save.hpp"
#include "../../../../tests/support/async_mesh_edit.hpp"
#include "../../../../tests/support/async_prefab_edit.hpp"
#include "asset_browser_model.hpp"
#include "asset_import_controller.hpp"
#include "commands/editor_command_history.hpp"
#include "documents/scene_document.hpp"
#include "editor_project.hpp"
#include "engine/core/error.hpp"
#include "engine/core/guid.hpp"
#include "engine/core/rid.hpp"
#include "engine/core/scene_types.hpp"
#include "engine/function/world/world.hpp"
#include "runtime/application.hpp"

#include "engine/core/error.hpp"
#include "engine/core/math/transform.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace gneiss::host;

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

[[nodiscard]] gneiss::engine::error
create_application(const std::filesystem::path& asset_root,
                   std::shared_ptr<gneiss::runtime::application>& output) {
  const auto root = asset_root.generic_string();
  gneiss::runtime::application_configuration desc;
  desc.asset_root = root;
  return gneiss::engine::error{gneiss::runtime::application::create(desc, output)};
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

[[nodiscard]] std::size_t count_prefab_name(const gneiss::editor::scene_document& session,
                                            std::string_view name) {
  return static_cast<std::size_t>(std::ranges::count_if(
      session.prefab_nodes(), [name](const auto& node) { return node.display_name == name; }));
}

} // namespace

int main() try { // NOLINT(bugprone-exception-escape): 测试入口统一返回失败码。
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-lantern-workflow-" + std::to_string(suffix));
  const temporary_project cleanup{root};
  std::filesystem::copy(GNEISS_EDITOR_LANTERN_PROJECT, root,
                        std::filesystem::copy_options::recursive);

  gneiss::host::editor_project project;
  if (gneiss::host::load_editor_project(root, project) != gneiss::engine::error::success) {
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

  std::shared_ptr<gneiss::runtime::application> application;
  gneiss::engine::world* world = nullptr;
  gneiss::editor::scene_document session;
  if (create_application(project.asset_root, application) != gneiss::engine::error::success ||
      (world = application->native_world()) == nullptr ||
      gneiss::test_support::open_document(session, *application, world, project.startup_scene) !=
          gneiss::engine::error::success) {
    return 8;
  }
  constexpr std::string_view prefab_uri = "asset://prefabs/lantern.prefab.json";
  const std::array instance_uuids{
      *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000010"),
      *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000020"),
      *gneiss::engine::guid::parse("20000000-0000-4000-8000-000000000030")};
  if (session.nodes().size() != 7U || session.prefab_nodes().size() != 15U ||
      count_prefab_name(session, "Lantern Glass") != 3U ||
      std::ranges::any_of(session.nodes(), [](const auto& current) {
        return current.display_name.empty() || current.display_name == current.uuid.string();
      })) {
    return 9;
  }
  for (const auto instance_uuid : instance_uuids) {
    const auto* root_node = session.find_prefab_root(instance_uuid);
    if (root_node == nullptr || !root_node->is_instance_root || root_node->is_read_only ||
        root_node->prefab_uri != prefab_uri) {
      return 9;
    }
  }
  const auto* left = session.find_prefab_root(instance_uuids[0]);
  const auto* center = session.find_prefab_root(instance_uuids[1]);
  const auto* right = session.find_prefab_root(instance_uuids[2]);
  if (left->local_transform.translation[0] != -3.0F ||
      center->local_transform.translation[2] != -0.8F ||
      right->local_transform.translation[0] != 3.0F) {
    return 9;
  }

  const auto prefab_path = project.asset_root / "prefabs" / "lantern.prefab.json";
  auto prefab_text = read_text(prefab_path);
  const auto old_name = prefab_text.find("Lantern Glass");
  if (old_name == std::string::npos) {
    return 9;
  }
  prefab_text.replace(old_name, std::string_view{"Lantern Glass"}.size(), "Lantern Glass Updated");
  write_text(prefab_path, prefab_text);
  gneiss::editor::editor_command_history refresh_history;
  for (const auto instance_uuid : instance_uuids) {
    const auto* root_node = session.find_prefab_root(instance_uuid);
    if (!root_node || gneiss::test_support::refresh_prefab_document(
                          *application, session, refresh_history, root_node->node)
                          .failed())
      return 9;
  }
  if (count_prefab_name(session, "Lantern Glass Updated") != 3U)
    return 9;
  for (std::size_t i = 0; i < instance_uuids.size(); ++i)
    if (gneiss::test_support::finish_history(refresh_history, refresh_history.undo()).failed())
      return 9;
  if (count_prefab_name(session, "Lantern Glass") != 3U)
    return 9;
  for (std::size_t i = 0; i < instance_uuids.size(); ++i)
    if (gneiss::test_support::finish_history(refresh_history, refresh_history.redo()).failed())
      return 9;
  gneiss::editor::editor_command_history history;
  gneiss::scene_node_id node;
  if (gneiss::test_support::create_mesh_document(*application, session, "Imported Lantern",
                                                 mesh->asset_uri, material->asset_uri, node,
                                                 &history) != gneiss::engine::error::success) {
    return 10;
  }
  const auto* created = session.selected_node();
  const gneiss::editor::scene_node_snapshot snapshot{.uuid = created->uuid,
                                                     .parent_uuid = {},
                                                     .display_name = created->display_name,
                                                     .mesh_uri = created->mesh_uri,
                                                     .material_uri = created->material_uri};
  if (history.undo() != gneiss::engine::error::success ||
      session.find_node(snapshot.uuid) != nullptr ||
      history.redo() != gneiss::engine::error::success ||
      session.find_node(snapshot.uuid) == nullptr ||
      gneiss::test_support::save_document(session, project.asset_root) !=
          gneiss::engine::error::success) {
    return 11;
  }
  session.close();

  gneiss::editor::scene_document reloaded;
  if (gneiss::test_support::open_document(reloaded, *application, world, project.startup_scene) !=
          gneiss::engine::error::success ||
      reloaded.find_node(snapshot.uuid) == nullptr || reloaded.prefab_nodes().size() != 15U ||
      count_prefab_name(reloaded, "Lantern Glass Updated") != 3U) {
    return 12;
  }
  reloaded.close();
  history.clear();
  refresh_history.clear();
  application.reset();
  return 0;
} catch (...) {
  return 99;
}
