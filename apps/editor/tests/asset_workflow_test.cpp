// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "asset_reimport_queue.h"
#include "editor_session.h"
#include "package_archive.h"
#include "project_workspace.h"
#include "runtime_process.h"

#include <gneiss/application.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

namespace {
struct temporary_project {
  std::filesystem::path root;
  ~temporary_project() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }
};
std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
template <typename Predicate>
bool wait_for(gneiss::editor::runtime_process& process, Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    process.update();
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}
}

int main() try {
  using namespace gneiss;
  using namespace gneiss::editor;
  temporary_project temporary{
      std::filesystem::temp_directory_path() /
          ("gneiss-asset-workflow-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())),
  };
  editor_project project;
  if (create_editor_project(temporary.root / "project", GNEISS_TEST_TEMPLATE, "Asset Workflow",
                            project) != result::success) {
    return 1;
  }
#if defined(_WIN32)
  const auto module_name = project.game_module.name + ".dll";
#elif defined(__APPLE__)
  const auto module_name = "lib" + project.game_module.name + ".dylib";
#else
  const auto module_name = "lib" + project.game_module.name + ".so";
#endif
  const auto module =
      project.project_root / project.game_module.profiles[0].directory / module_name;
  std::filesystem::create_directories(module.parent_path());
  std::filesystem::copy_file(GNEISS_TEST_MODULE, module);
  const auto imported =
      import_external_asset(project.project_root, project.asset_root, GNEISS_TEST_SOURCE);
  if (imported.result != editor_import_result::success) {
    return 3;
  }
  const auto& uris = imported.import.output_uris;
  const auto mesh =
      std::ranges::find_if(uris, [](const auto& uri) { return uri.ends_with(".gneiss-mesh"); });
  const auto material =
      std::ranges::find_if(uris, [](const auto& uri) { return uri.ends_with(".material.json"); });
  if (mesh == uris.end() || material == uris.end()) {
    return 4;
  }
  const auto material_path = project.asset_root / std::filesystem::path(material->substr(8U));
  const auto previous_material = read_text(material_path);
  application app;
  gneiss_application_desc desc = GNEISS_APPLICATION_DESC_INIT;
  const auto asset_root = project.asset_root.generic_string();
  desc.asset_root = asset_root.data();
  desc.asset_root_length = static_cast<std::uint32_t>(asset_root.size());
  gneiss_world world{};
  editor_session session;
  scene_node_id node;
  if (application::create(desc, app) != result::success ||
      app.get_world(world) != result::success ||
      session.open(app.get(), world, project.startup_scene) != result::success ||
      session.create_mesh_renderer_node("Imported", *mesh, *material, node) != result::success ||
      session.save(project.asset_root) != result::success) {
    return 5;
  }
  session.close();
  app.reset();
  runtime_process runtime;
  if (runtime.start(GNEISS_TEST_RUNTIME, {project.project_root}) != result::success ||
      !wait_for(runtime,
                [&] { return runtime.control_state() == runtime_control_state::running; })) {
    std::cerr << runtime.output() << "\nstate=" << static_cast<int>(runtime.control_state())
              << " result=" << runtime.last_result().message() << "\n";
    return 6;
  }
  auto source = read_text(imported.source_path);
  const auto color = source.find("0.5, 0.6, 0.7");
  if (color == std::string::npos) {
    return 7;
  }
  source.replace(color, 13U, "0.9, 0.2, 0.1");
  std::ofstream(imported.source_path) << source;
  asset_reimport_queue queue({
      .debounce = std::chrono::milliseconds(0),
      .stable_read_delay = std::chrono::milliseconds(1),
  });
  queue.request_rescan(); // 模拟源文件事件丢失，通过补扫恢复同一导入事务。
  const auto now = asset_reimport_queue::clock::now();
  (void)queue.tick(project.project_root, project.asset_root, now);
  if (queue.tick(project.project_root, project.asset_root, now + std::chrono::milliseconds(2)) !=
          1U ||
      read_text(material_path) == previous_material) {
    return 8;
  }
  std::vector<asset_reimport_event> events;
  (void)queue.poll_events(events);
  const auto succeeded = std::ranges::find_if(
      events, [](const auto& event) { return event.state == asset_reimport_state::succeeded; });
  if (succeeded == events.end() ||
      runtime.publish_asset_revision(succeeded->import.import.output_uris) != result::success ||
      !wait_for(runtime,
                [&] {
                  return runtime.asset_reload_status().state == runtime_asset_reload_state::applied;
                }) ||
      runtime.request_stop() != result::success ||
      !wait_for(runtime, [&] { return !runtime.is_running(); })) {
    return 9;
  }
  const auto package = temporary.root / "package";
  if (export_editor_project(project,
                            {
                                .runtime_executable = GNEISS_TEST_RUNTIME,
                                .output_root = package,
                                .profile = app::game_build_profile::debug,
                                .create_zip = false,
                                .asset_progress = {},
                            }) != result::success ||
      verify_package_manifest(package) != result::success) {
    return 10;
  }
  if (runtime.start(package / "bin" / std::filesystem::path(GNEISS_TEST_RUNTIME).filename(),
                    {package}) != result::success ||
      !wait_for(runtime,
                [&] { return runtime.control_state() == runtime_control_state::running; }) ||
      runtime.request_stop() != result::success ||
      !wait_for(runtime, [&] { return !runtime.is_running(); }) || runtime.exit_code() != 0) {
    return 11;
  }
  return 0;
} catch (...) {
  return 99;
}
