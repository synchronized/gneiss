// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "engine/core/error.hpp"
#include "package_archive.hpp"
#include "project_workspace.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace gneiss::host;

int main() try {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-project-workspace-test-" + std::to_string(suffix));
  const auto project_root = root / "sample";
  const auto state_file = root / "config" / "editor.json";
  gneiss::host::editor_project project;
  const auto create_result = gneiss::host::create_editor_project(
      project_root, GNEISS_EDITOR_TEST_TEMPLATE, "Sample Project", project);
  if (create_result != gneiss::engine::error::success) {
    std::cerr << "create failed: " << create_result.message() << " root=" << project_root
              << " template=" << GNEISS_EDITOR_TEST_TEMPLATE << '\n';
    return 1;
  }
  if (project.name != "Sample Project" ||
      project.startup_scene != "asset://scenes/main.scene.json" ||
      !std::filesystem::is_directory(project_root / "sources") ||
      !std::filesystem::is_regular_file(project_root / "assets/scenes/main.scene.json")) {
    return 8;
  }
  static constexpr unsigned char png[] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
      0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00,
      0x00, 0xB5, 0x1C, 0x0C, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x49, 0x44, 0x41, 0x54, 0x78,
      0xDA, 0x63, 0x64, 0xF8, 0x0F, 0x00, 0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xE3, 0x66,
      0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
  std::filesystem::create_directories(project_root / "assets" / "textures");
  std::ofstream authored_texture(project_root / "assets" / "textures" / "test.png",
                                 std::ios::binary | std::ios::trunc);
  authored_texture.write(reinterpret_cast<const char*>(png), sizeof(png));
  authored_texture.close();
  std::ofstream(project_root / "assets" / "textures" / "test.texture.json")
      << R"({"format":"gneiss.texture","version":2,"source":"asset://textures/test.png","color_space":"srgb","usage":"default"})";
  if (gneiss::host::create_editor_project(project_root, "Duplicate", project) !=
      gneiss::engine::error::invalid_state) {
    return 9;
  }
#if defined(_WIN32)
  const auto runtime_name = "gneiss_runtime.exe";
  const auto dependency_name = "gneiss_engine.dll";
#elif defined(__APPLE__)
  const auto runtime_name = "gneiss_runtime";
  const auto dependency_name = "libgneiss_engine.dylib";
#else
  const auto runtime_name = "gneiss_runtime";
  const auto dependency_name = "libgneiss_engine.so";
#endif
  const auto runtime_root = root / "sdk" / "bin";
  std::filesystem::create_directories(runtime_root);
  std::ofstream(runtime_root / runtime_name) << "runtime";
  std::ofstream(runtime_root / dependency_name) << "dependency";
  const auto package_root = root / "package-a";
  std::uint64_t progress_count{};
  gneiss::host::project_export_options export_options{
      .runtime_executable = runtime_root / runtime_name,
      .output_root = package_root,
      .profile = gneiss::host::project_build_profile::debug,
      .create_zip = true,
      .asset_progress = {}};
  export_options.asset_progress = [&progress_count](std::uint64_t current, std::uint64_t total,
                                                    std::string_view, bool) {
    if (current > 0U && current <= total) {
      ++progress_count;
    }
  };
  gneiss::host::project_export_report export_report;
  const auto export_result =
      gneiss::host::export_editor_project(project, export_options, export_report);
  if (export_result != gneiss::engine::error::success) {
    std::cerr << "export failed: " << export_result.message() << '\n';
    return 7;
  }
  if (!std::filesystem::is_regular_file(package_root / "gneiss.project.json") ||
      !std::filesystem::is_regular_file(package_root / "assets" / "scenes" / "main.scene.json") ||
      !std::filesystem::is_regular_file(package_root / "assets" / ".gneiss-build.json") ||
      !std::filesystem::is_regular_file(package_root / "assets" / "textures" /
                                        "test.gneiss-texture") ||
      std::filesystem::exists(package_root / "assets" / "textures" / "test.png") ||
      !std::filesystem::is_regular_file(package_root / "bin" / runtime_name) ||
      !std::filesystem::is_regular_file(package_root / "bin" / dependency_name) ||
      !std::filesystem::is_regular_file(package_root / "run.cmd") ||
      !std::filesystem::is_regular_file(package_root / "run.sh") ||
      !std::filesystem::is_regular_file(package_root / "gneiss.package.json") ||
      !std::filesystem::is_regular_file(root / "package-a.zip") ||
      std::filesystem::exists(package_root / "sources") ||
      std::filesystem::exists(package_root / "CMakeLists.txt") ||
      progress_count != export_report.asset_source_count || export_report.asset_built_count == 0U ||
      gneiss::host::export_editor_project(project, runtime_root / runtime_name, package_root) !=
          gneiss::engine::error::invalid_state) {
    return 7;
  }
  std::ifstream cooked_texture_description(
      package_root / "assets" / "textures" / "test.texture.json", std::ios::binary);
  const std::string cooked_texture_text{std::istreambuf_iterator<char>(cooked_texture_description),
                                        std::istreambuf_iterator<char>()};
  cooked_texture_description.close();
  if (cooked_texture_text.find("asset://textures/test.gneiss-texture") == std::string::npos ||
      cooked_texture_text.find("test.png") != std::string::npos) {
    return 7;
  }
  const auto second_package = root / "package-b";
  if (gneiss::host::export_editor_project(
          project, {.runtime_executable = runtime_root / runtime_name,
                    .output_root = second_package,
                    .profile = gneiss::host::project_build_profile::debug,
                    .create_zip = true,
                    .asset_progress = {}}) != gneiss::engine::error::success) {
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
      manifest_text.find("d92c6a81b2ff50096bcda80885427d1f59a25b5f483f7055523504925d16ab23") ==
          std::string::npos) {
    return 11;
  }
  if (gneiss::host::verify_package_manifest(package_root) != gneiss::engine::error::success) {
    return 12;
  }
  std::ofstream(package_root / "bin" / runtime_name, std::ios::binary | std::ios::trunc)
      << "tampered runtime";
  if (gneiss::host::verify_package_manifest(package_root) !=
      gneiss::engine::error::dependency_failed) {
    return 13;
  }
  if (gneiss::host::remember_recent_project(state_file, project) !=
      gneiss::engine::error::success) {
    return 2;
  }
  std::vector<gneiss::host::editor_project> recent;
  if (gneiss::host::load_recent_projects(state_file, recent) != gneiss::engine::error::success ||
      recent.size() != 1U || recent.front().project_root != project.project_root) {
    return 3;
  }
  if (gneiss::host::remember_recent_project(state_file, project) !=
          gneiss::engine::error::success ||
      gneiss::host::load_recent_projects(state_file, recent) != gneiss::engine::error::success ||
      recent.size() != 1U) {
    return 4;
  }
  // 替换失败仍保留原配置；Windows 禁止删除共享用于稳定复现。
#if defined(_WIN32)
  const auto held = CreateFileW(state_file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (held == INVALID_HANDLE_VALUE)
    return 20;
  const auto failed_save = remember_recent_project(state_file, project);
  CloseHandle(held);
  if (failed_save != gneiss::engine::error::io ||
      load_recent_projects(state_file, recent).failed() || recent.size() != 1U ||
      std::filesystem::exists(state_file.string() + ".tmp"))
    return 21;
#endif
  std::filesystem::remove_all(project_root);
  if (gneiss::host::load_recent_projects(state_file, recent) != gneiss::engine::error::success ||
      !recent.empty()) {
    return 5;
  }
  std::filesystem::remove_all(root);
  return 0;
} catch (const std::exception& exception) {
  std::cerr << "unexpected exception: " << exception.what() << '\n';
  return 99;
}
