// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_project.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] std::string project_json(std::string_view asset_root,
                                       std::string_view startup_scene) {
  return "{\n  \"format\": \"gneiss.project\",\n  \"version\": 1,\n  "
         "\"name\": \"Test Project\",\n  \"asset_root\": \"" +
         std::string(asset_root) + "\",\n  \"startup_scene\": \"" + std::string(startup_scene) +
         "\"\n}\n";
}

} // namespace

int main() try {
  gneiss::editor::editor_project project;
  const std::filesystem::path demo = GNEISS_EDITOR_DEMO_PROJECT;
  if (gneiss::editor::load_editor_project(demo, project) != gneiss::result::success ||
      project.name != "Gneiss Editor Demo" ||
      project.startup_scene != "asset://scenes/main.scene.json" ||
      gneiss::editor::load_editor_project(demo / "gneiss.project.json", project) !=
          gneiss::result::success) {
    return 1;
  }

  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-editor-project-test-" + std::to_string(suffix));
  std::filesystem::create_directories(root / "assets" / "scenes");
  if (!write_text(root / "assets" / "scenes" / "main.scene.json", "{}") ||
      !write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://scenes/main.scene.json")) ||
      gneiss::editor::load_editor_project(root, project) != gneiss::result::success) {
    return 2;
  }

  const auto previous_name = project.name;
  if (gneiss::editor::load_editor_project(root / "missing", project) != gneiss::result::not_found ||
      project.name != previous_name) {
    return 3;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("../outside", "asset://scenes/main.scene.json")) ||
      gneiss::editor::load_editor_project(root, project) != gneiss::result::invalid_argument) {
    return 4;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("missing-assets", "asset://scenes/main.scene.json")) ||
      gneiss::editor::load_editor_project(root, project) != gneiss::result::not_found) {
    return 5;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://scenes/missing.scene.json")) ||
      gneiss::editor::load_editor_project(root, project) != gneiss::result::not_found) {
    return 6;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://../outside.scene.json")) ||
      gneiss::editor::load_editor_project(root, project) != gneiss::result::invalid_argument) {
    return 7;
  }
  if (!write_text(root / "gneiss.project.json", "{invalid") ||
      gneiss::editor::load_editor_project(root, project) != gneiss::result::invalid_argument) {
    return 8;
  }
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
