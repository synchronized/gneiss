// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "project_workspace.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main() try {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-project-workspace-test-" + std::to_string(suffix));
  const auto project_root = root / "sample";
  const auto state_file = root / "config" / "editor.json";
  gneiss::editor::editor_project project;
  if (gneiss::editor::create_editor_project(project_root, "Sample Project", project) !=
          gneiss::result::success ||
      project.name != "Sample Project" ||
      project.startup_scene != "asset://scenes/main.scene.json" ||
      !std::filesystem::is_directory(project_root / "sources") ||
      gneiss::editor::create_editor_project(project_root, "Duplicate", project) !=
          gneiss::result::invalid_state) {
    return 1;
  }
  if (gneiss::editor::remember_recent_project(state_file, project) != gneiss::result::success) {
    return 2;
  }
  std::vector<gneiss::editor::editor_project> recent;
  if (gneiss::editor::load_recent_projects(state_file, recent) != gneiss::result::success ||
      recent.size() != 1U || recent.front().project_root != project.project_root) {
    return 3;
  }
  if (gneiss::editor::remember_recent_project(state_file, project) != gneiss::result::success ||
      gneiss::editor::load_recent_projects(state_file, recent) != gneiss::result::success ||
      recent.size() != 1U) {
    return 4;
  }
  std::filesystem::remove_all(project_root);
  if (gneiss::editor::load_recent_projects(state_file, recent) != gneiss::result::success ||
      !recent.empty()) {
    return 5;
  }
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
