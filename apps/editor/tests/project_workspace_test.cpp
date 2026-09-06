// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "package_archive.h"
#include "project_workspace.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  const auto create_result = gneiss::editor::create_editor_project(
      project_root, GNEISS_EDITOR_TEST_TEMPLATE, "Sample Project", project);
  if (create_result != gneiss::result::success) {
    std::cerr << "create failed: " << create_result.message() << " root=" << project_root
              << " template=" << GNEISS_EDITOR_TEST_TEMPLATE << '\n';
    return 1;
  }
  if (project.name != "Sample Project" ||
      project.startup_scene != "asset://scenes/main.scene.json" ||
      !std::filesystem::is_directory(project_root / "sources") ||
      !std::filesystem::is_regular_file(project_root / "CMakePresets.json") ||
      !std::filesystem::is_regular_file(project_root / "game_module.cpp")) {
    return 8;
  }
  if (gneiss::editor::create_editor_project(project_root, "Duplicate", project) !=
      gneiss::result::invalid_state) {
    return 9;
  }
  std::ifstream module(project_root / "game_module.cpp", std::ios::binary);
  const std::string module_text{std::istreambuf_iterator<char>(module),
                                std::istreambuf_iterator<char>()};
  module.close();
  if (project.game_module.name != "gneiss_game" ||
      project.game_module.profiles[0].directory != "modules/debug" ||
      project.game_module.profiles[1].directory != "modules/development" ||
      project.game_module.profiles[2].directory != "modules/shipping" ||
      module_text.find("gneiss.game.sample.project") == std::string::npos ||
      module_text.find("gneiss.template.game") != std::string::npos) {
    return 6;
  }
  const auto& debug_profile = project.game_module.profiles[0];
  std::filesystem::create_directories(project_root / debug_profile.directory);
#if defined(_WIN32)
  const auto module_name = "gneiss_game.dll";
  const auto runtime_name = "gneiss_runtime.exe";
  const auto dependency_name = "gneiss_engine.dll";
#elif defined(__APPLE__)
  const auto module_name = "libgneiss_game.dylib";
  const auto runtime_name = "gneiss_runtime";
  const auto dependency_name = "libgneiss_engine.dylib";
#else
  const auto module_name = "libgneiss_game.so";
  const auto runtime_name = "gneiss_runtime";
  const auto dependency_name = "libgneiss_engine.so";
#endif
  std::ofstream(project_root / debug_profile.directory / module_name) << "module";
  const auto runtime_root = root / "sdk" / "bin";
  std::filesystem::create_directories(runtime_root);
  std::ofstream(runtime_root / runtime_name) << "runtime";
  std::ofstream(runtime_root / dependency_name) << "dependency";
  const auto package_root = root / "package-a";
  std::uint64_t progress_count{};
  gneiss::editor::project_export_options export_options{
      .runtime_executable = runtime_root / runtime_name,
      .output_root = package_root,
      .profile = gneiss::app::game_build_profile::debug,
      .create_zip = true};
  export_options.asset_progress = [&progress_count](std::uint64_t current, std::uint64_t total,
                                                    std::string_view, bool) {
    if (current > 0U && current <= total) {
      ++progress_count;
    }
  };
  gneiss::editor::project_export_report export_report;
  const auto export_result =
      gneiss::editor::export_editor_project(project, export_options, export_report);
  if (export_result != gneiss::result::success) {
    std::cerr << "export failed: " << export_result.message() << '\n';
    return 7;
  }
  if (!std::filesystem::is_regular_file(package_root / "gneiss.project.json") ||
      !std::filesystem::is_regular_file(package_root / "assets" / "scenes" / "main.scene.json") ||
      !std::filesystem::is_regular_file(package_root / "assets" / ".gneiss-build.json") ||
      !std::filesystem::is_regular_file(package_root / "modules" / "debug" / module_name) ||
      !std::filesystem::is_regular_file(package_root / "bin" / runtime_name) ||
      !std::filesystem::is_regular_file(package_root / "bin" / dependency_name) ||
      !std::filesystem::is_regular_file(package_root / "run.cmd") ||
      !std::filesystem::is_regular_file(package_root / "run.sh") ||
      !std::filesystem::is_regular_file(package_root / "gneiss.package.json") ||
      !std::filesystem::is_regular_file(root / "package-a.zip") ||
      std::filesystem::exists(package_root / "sources") ||
      std::filesystem::exists(package_root / "CMakeLists.txt") ||
      progress_count != export_report.asset_source_count || export_report.asset_built_count == 0U ||
      gneiss::editor::export_editor_project(project, runtime_root / runtime_name, package_root) !=
          gneiss::result::invalid_state) {
    return 7;
  }
  const auto second_package = root / "package-b";
  if (gneiss::editor::export_editor_project(project,
                                            {.runtime_executable = runtime_root / runtime_name,
                                             .output_root = second_package,
                                             .profile = gneiss::app::game_build_profile::debug,
                                             .create_zip = true}) != gneiss::result::success) {
    return 10;
  }
  std::ifstream first_zip(root / "package-a.zip", std::ios::binary);
  std::ifstream second_zip(root / "package-b.zip", std::ios::binary);
  std::ifstream manifest(package_root / "gneiss.package.json", std::ios::binary);
  const std::string first_zip_bytes{std::istreambuf_iterator<char>(first_zip),
                                    std::istreambuf_iterator<char>()};
  const std::string second_zip_bytes{std::istreambuf_iterator<char>(second_zip),
                                     std::istreambuf_iterator<char>()};
  const std::string manifest_text{std::istreambuf_iterator<char>(manifest),
                                  std::istreambuf_iterator<char>()};
  first_zip.close();
  second_zip.close();
  manifest.close();
  if (first_zip_bytes.empty() || first_zip_bytes != second_zip_bytes ||
      manifest_text.find("\"profile\": \"debug\"") == std::string::npos ||
      manifest_text.find("120970d812836f19888625587a4606a5ad23cef31c8684e601771552548fc6b9") ==
          std::string::npos) {
    return 11;
  }
  if (gneiss::editor::verify_package_manifest(package_root) != gneiss::result::success) {
    return 12;
  }
  std::ofstream(package_root / "modules" / "debug" / module_name, std::ios::app) << "tampered";
  if (gneiss::editor::verify_package_manifest(package_root) != gneiss::result::dependency_failed) {
    return 13;
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
} catch (const std::exception& exception) {
  std::cerr << "unexpected exception: " << exception.what() << '\n';
  return 99;
}
