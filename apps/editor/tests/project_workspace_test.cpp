// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

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
      module_text.find("gneiss.game.sample.project") == std::string::npos ||
      module_text.find("gneiss.template.game") != std::string::npos) {
    return 6;
  }
  std::filesystem::create_directories(project_root / "modules");
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
  std::ofstream(project_root / "modules" / module_name) << "module";
  const auto runtime_root = root / "sdk" / "bin";
  std::filesystem::create_directories(runtime_root);
  std::ofstream(runtime_root / runtime_name) << "runtime";
  std::ofstream(runtime_root / dependency_name) << "dependency";
  const auto package_root = root / "package";
  if (gneiss::editor::export_editor_project(project, runtime_root / runtime_name, package_root) !=
          gneiss::result::success ||
      !std::filesystem::is_regular_file(package_root / "gneiss.project.json") ||
      !std::filesystem::is_regular_file(package_root / "assets" / "scenes" / "main.scene.json") ||
      !std::filesystem::is_regular_file(package_root / "modules" / module_name) ||
      !std::filesystem::is_regular_file(package_root / "bin" / runtime_name) ||
      !std::filesystem::is_regular_file(package_root / "bin" / dependency_name) ||
      !std::filesystem::is_regular_file(package_root / "run.cmd") ||
      !std::filesystem::is_regular_file(package_root / "run.sh") ||
      std::filesystem::exists(package_root / "sources") ||
      std::filesystem::exists(package_root / "CMakeLists.txt") ||
      gneiss::editor::export_editor_project(project, runtime_root / runtime_name, package_root) !=
          gneiss::result::invalid_state) {
    return 7;
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
