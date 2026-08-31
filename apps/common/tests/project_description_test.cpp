// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include <gneiss/app/project_description.h>

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

[[nodiscard]] std::string project_json(std::string_view asset_root, std::string_view startup_scene,
                                       std::string_view game_module = {}) {
  return "{\n  \"format\": \"gneiss.project\",\n  \"version\": " +
         std::string(game_module.empty() ? "1" : "2") +
         ",\n  "
         "\"name\": \"Test Project\",\n  \"asset_root\": \"" +
         std::string(asset_root) + "\",\n  \"startup_scene\": \"" + std::string(startup_scene) +
         "\"" + std::string(game_module) + "\n}\n";
}

} // namespace

int main() try {
  gneiss::app::project_description project;
  gneiss::app::project_load_report report;
  const std::filesystem::path demo = GNEISS_APP_TEST_PROJECT;
  if (gneiss::app::load_project_description(demo, project) != gneiss::result::success ||
      project.name != "Gneiss Editor Demo" ||
      project.startup_scene != "asset://scenes/main.scene.json" ||
      gneiss::app::load_project_description(demo / "gneiss.project.json", project) !=
          gneiss::result::invalid_argument) {
    return 1;
  }

  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-app-project-test-" + std::to_string(suffix));
  std::filesystem::create_directories(root / "assets" / "scenes");
  if (!write_text(root / "assets" / "scenes" / "main.scene.json", "{}") ||
      !write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://scenes/main.scene.json")) ||
      gneiss::app::load_project_description(root, project) != gneiss::result::success) {
    return 2;
  }

#if defined(_WIN32)
  constexpr std::string_view module_filename = "test_game.dll";
#elif defined(__APPLE__)
  constexpr std::string_view module_filename = "libtest_game.dylib";
#else
  constexpr std::string_view module_filename = "libtest_game.so";
#endif
  std::filesystem::create_directories(root / "modules");
  if (!write_text(root / "modules" / module_filename, "fixture") ||
      !write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://scenes/main.scene.json",
                               ",\n  \"game_module\": {\"name\": \"test_game\", "
                               "\"directory\": \"modules\", \"build_preset\": \"game-debug\", "
                               "\"build_target\": \"test_game\"}")) ||
      gneiss::app::load_project_description(root, project, report) != gneiss::result::success ||
      project.game_module.name != "test_game" || project.game_module.build_preset != "game-debug") {
    return 3;
  }
  std::filesystem::path module_path;
  if (gneiss::app::resolve_game_module_path(project, module_path) != gneiss::result::success ||
      module_path.filename() != module_filename) {
    return 3;
  }

  const auto previous_name = project.name;
  if (gneiss::app::load_project_description(root / "missing", project, report) !=
          gneiss::result::not_found ||
      report.stage != gneiss::app::project_load_stage::project_root ||
      gneiss::app::project_load_stage_name(report.stage) != "project_root" ||
      project.name != previous_name) {
    return 4;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("../outside", "asset://scenes/main.scene.json")) ||
      gneiss::app::load_project_description(root, project) != gneiss::result::invalid_argument) {
    return 5;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("missing-assets", "asset://scenes/main.scene.json")) ||
      gneiss::app::load_project_description(root, project) != gneiss::result::not_found) {
    return 6;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://scenes/missing.scene.json")) ||
      gneiss::app::load_project_description(root, project) != gneiss::result::not_found) {
    return 7;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://../outside.scene.json")) ||
      gneiss::app::load_project_description(root, project) != gneiss::result::invalid_argument) {
    return 8;
  }
  if (!write_text(root / "gneiss.project.json", "{invalid") ||
      gneiss::app::load_project_description(root, project, report) !=
          gneiss::result::invalid_argument ||
      report.operation != gneiss::result::invalid_argument ||
      report.stage != gneiss::app::project_load_stage::document ||
      report.context.filename() != "gneiss.project.json") {
    return 9;
  }
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
