// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include <gneiss/app/project_description.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace gneiss::host;

namespace {

[[nodiscard]] bool write_text(const std::filesystem::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(stream);
}

[[nodiscard]] std::string project_json(std::string_view asset_root, std::string_view startup_scene,
                                       std::string_view extra_fields = {}) {
  return "{\n  \"format\": \"gneiss.project\",\n  \"version\": " + std::string{"5"} +
         ",\n  "
         "\"name\": \"Test Project\",\n  \"asset_root\": \"" +
         std::string(asset_root) + "\",\n  \"startup_scene\": \"" + std::string(startup_scene) +
         "\",\"asset_build\":{\"retain\":[]}" + std::string(extra_fields) + "\n}\n";
}

} // namespace

int main() try {
  gneiss::host::project_description project;
  gneiss::host::project_load_report report;
  const std::filesystem::path demo = GNEISS_APP_TEST_PROJECT;
  if (gneiss::host::load_project_description(demo, project) != gneiss::engine::error::success ||
      project.name != "Gneiss Editor Demo" ||
      project.startup_scene != "asset://scenes/main.scene.json" ||
      gneiss::host::load_project_description(demo / "gneiss.project.json", project) !=
          gneiss::engine::error::invalid_argument) {
    return 1;
  }

  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-app-project-test-" + std::to_string(suffix));
  std::filesystem::create_directories(root / "assets" / "scenes");
  std::filesystem::create_directories(root / "assets" / "input");
  if (!write_text(root / "assets" / "scenes" / "main.scene.json", "{}") ||
      !write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://scenes/main.scene.json")) ||
      gneiss::host::load_project_description(root, project) != gneiss::engine::error::success) {
    return 2;
  }

  if (!write_text(root / "assets" / "input" / "default.input-map.json", "{}") ||
      !write_text(
          root / "gneiss.project.json",
          R"({"format":"gneiss.project","version":5,"name":"Test Project","asset_root":"assets","startup_scene":"asset://scenes/main.scene.json","input_map":"asset://input/default.input-map.json","asset_build":{"retain":[]}})") ||
      gneiss::host::load_project_description(root, project, report).failed() ||
      project.input_map != "asset://input/default.input-map.json")
    return 3;

  if (!write_text(root / "assets" / "studio.gneiss-environment", "fixture") ||
      !write_text(
          root / "gneiss.project.json",
          R"({"format":"gneiss.project","version":5,"name":"Test Project","asset_root":"assets","startup_scene":"asset://scenes/main.scene.json","environment":{"asset":"asset://studio.gneiss-environment","intensity":0.75,"rotation_degrees":30},"asset_build":{"retain":[]}})") ||
      gneiss::host::load_project_description(root, project, report) !=
          gneiss::engine::error::success ||
      project.environment.asset != "asset://studio.gneiss-environment" ||
      project.environment.intensity != 0.75F || project.environment.rotation_degrees != 30.0F) {
    return 4;
  }

  if (!write_text(
          root / "gneiss.project.json",
          R"({"format":"gneiss.project","version":5,"name":"Test Project","asset_root":"assets","startup_scene":"asset://scenes/main.scene.json","asset_build":{"retain":["asset://studio.gneiss-environment"]}})") ||
      gneiss::host::load_project_description(root, project, report) !=
          gneiss::engine::error::success ||
      project.retained_assets != std::vector<std::string>{"asset://studio.gneiss-environment"}) {
    return 4;
  }
  if (!write_text(
          root / "gneiss.project.json",
          R"({"format":"gneiss.project","version":5,"name":"Test Project","asset_root":"assets","startup_scene":"asset://scenes/main.scene.json","asset_build":{"retain":["asset://studio.gneiss-environment","asset://studio.gneiss-environment"]}})") ||
      gneiss::host::load_project_description(root, project, report) !=
          gneiss::engine::error::invalid_argument ||
      report.stage != gneiss::host::project_load_stage::asset_build) {
    return 4;
  }

  const auto previous_name = project.name;
  if (gneiss::host::load_project_description(root / "missing", project, report) !=
          gneiss::engine::error::not_found ||
      report.stage != gneiss::host::project_load_stage::project_root ||
      gneiss::host::project_load_stage_name(report.stage) != "project_root" ||
      project.name != previous_name) {
    return 5;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("../outside", "asset://scenes/main.scene.json")) ||
      gneiss::host::load_project_description(root, project) !=
          gneiss::engine::error::invalid_argument) {
    return 5;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("missing-assets", "asset://scenes/main.scene.json")) ||
      gneiss::host::load_project_description(root, project) != gneiss::engine::error::not_found) {
    return 6;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://scenes/missing.scene.json")) ||
      gneiss::host::load_project_description(root, project) != gneiss::engine::error::not_found) {
    return 7;
  }
  if (!write_text(root / "gneiss.project.json",
                  project_json("assets", "asset://../outside.scene.json")) ||
      gneiss::host::load_project_description(root, project) !=
          gneiss::engine::error::invalid_argument) {
    return 8;
  }
  if (!write_text(root / "gneiss.project.json", "{invalid") ||
      gneiss::host::load_project_description(root, project, report) !=
          gneiss::engine::error::invalid_argument ||
      report.operation != gneiss::engine::error::invalid_argument ||
      report.stage != gneiss::host::project_load_stage::document ||
      report.context.filename() != "gneiss.project.json") {
    return 9;
  }
  if (!write_text(
          root / "gneiss.project.json",
          R"({"format":"gneiss.project","version":5,"game_module":{},"asset_build":{"retain":[]}})") ||
      gneiss::host::load_project_description(root, project, report) !=
          gneiss::engine::error::unsupported ||
      report.stage != gneiss::host::project_load_stage::schema)
    return 10;
  for (const auto version : {1, 2, 3, 4, 6}) {
    auto rejected = project_json("assets", "asset://scenes/main.scene.json");
    const auto offset = rejected.find("\"version\": 5");
    rejected.replace(offset, 12, "\"version\": " + std::to_string(version));
    const auto previous = project.name;
    if (!write_text(root / "gneiss.project.json", rejected) ||
        load_project_description(root, project, report) != gneiss::engine::error::unsupported ||
        report.stage != project_load_stage::schema || project.name != previous)
      return 11;
  }
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
