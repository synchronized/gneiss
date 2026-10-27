// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gneiss contributors

#include "editor_ui.hpp"
#include "engine/core/result.hpp"
#include "runtime/application.hpp"
#include <chrono>
#include <thread>

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

void create_test_context() {
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = nullptr;
  unsigned char* pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
}

bool draw_workspace_frame() {
  auto& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280.0F, 720.0F);
  io.DeltaTime = 1.0F / 60.0F;
  ImGui::NewFrame();
  gneiss::editor::begin_editor_workspace();
  const auto* dockspace = ImGui::DockBuilderGetNode(ImHashStr("GneissEditorDockSpace"));
  const auto has_transparent_center =
      dockspace != nullptr &&
      (dockspace->SharedFlags & ImGuiDockNodeFlags_PassthruCentralNode) != 0;
  for (const auto* name :
       {"Scene Hierarchy", "Asset Browser", "Scene View", "Inspector", "Console"}) {
    ImGui::Begin(name);
    ImGui::End();
  }
  ImGui::Render();
  return has_transparent_center;
}

} // namespace

int main() try {
  using namespace gneiss;
  using engine::result;
  const auto root = std::filesystem::temp_directory_path() /
                    ("gneiss-layout-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto root_text = root.generic_string();
  runtime::application_configuration config;
  config.user_root = root_text;
  config.asset_root = root_text;
  std::shared_ptr<runtime::application> app;
  if (runtime::application::create(config, app).failed())
    return 1;
  auto files = app->async_files();
  gneiss::engine::files::request_id forbidden;
  if (files->write(
          "asset://must-not-write", {}, {}, [](auto) {}, forbidden) != result::unsupported ||
      forbidden.value != 0)
    return 18;
  std::shared_ptr<const gneiss::engine::files::source_domain> asset_source, user_source;
  if (files->resolve_source("asset://example", asset_source).failed() ||
      files->resolve_source("user://example", user_source).failed() || asset_source == user_source)
    return 19;
  auto pump = [&](auto pending) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (pending()) {
      if (std::chrono::steady_clock::now() > deadline)
        return false;
      if (files->dispatch_completions(64).failed())
        return false;
      std::this_thread::yield();
    }
    return true;
  };
  editor::editor_panel_visibility visibility;
  editor::editor_layout layout;
  create_test_context();
  if (layout.initialize(files, "/project-a", visibility).failed() || !layout.loading() ||
      layout.advance(visibility) != result::not_ready || !draw_workspace_frame())
    return 2;
  result loaded;
  if (!pump([&] {
        loaded = layout.advance(visibility);
        return layout.loading();
      }) ||
      loaded.failed())
    return 3;
  visibility.console = false;
  visibility.asset_browser = false;
  if (layout.save(visibility).failed() || !layout.saving() ||
      layout.save(visibility) != result::not_ready || !pump([&] { return layout.saving(); }) ||
      layout.save_result().failed())
    return 4;
  const auto first_path = root / std::string(layout.uri().substr(7));
  auto read = [](const auto& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
  };
  const auto saved = read(first_path);
  if (!saved.starts_with("GNEISS_EDITOR_LAYOUT 2\nPANELS 10110\n"))
    return 5;
  ImGui::DestroyContext();
  create_test_context();
  if (layout.initialize(files, "/project-a", visibility).failed() || !visibility.console ||
      !pump([&] {
        loaded = layout.advance(visibility);
        return layout.loading();
      }) ||
      loaded.failed() || visibility.console || visibility.asset_browser)
    return 6;
  const auto ini_offset = saved.find('\n', std::string("GNEISS_EDITOR_LAYOUT 2\n").size());
  if (ini_offset == std::string::npos)
    return 15;
  std::ofstream(first_path, std::ios::binary | std::ios::trunc) << "GNEISS_EDITOR_LAYOUT 1\n"
                                                                << saved.substr(ini_offset + 1);
  if (layout.initialize(files, "/project-a", visibility).failed() || !pump([&] {
        loaded = layout.advance(visibility);
        return layout.loading();
      }) ||
      loaded.failed() || !visibility.console || !visibility.asset_browser)
    return 16;
  std::ofstream(first_path, std::ios::binary | std::ios::trunc) << saved;
  // Reset 优先于迟到的布局读取。
  if (layout.initialize(files, "/project-a", visibility).failed())
    return 7;
  editor::reset_editor_layout();
  if (!pump([&] {
        loaded = layout.advance(visibility);
        return layout.loading();
      }) ||
      !visibility.console)
    return 8;
  // 不同工程使用不同 URI；缺失配置保持默认。
  const std::string first_uri(layout.uri());
  if (layout.initialize(files, "/project-b", visibility).failed() || layout.uri() == first_uri ||
      !pump([&] {
        loaded = layout.advance(visibility);
        return layout.loading();
      }) ||
      loaded.failed() || !draw_workspace_frame() || layout.save(visibility).failed() ||
      !pump([&] { return layout.saving(); }) || layout.save_result().failed())
    return 9;
  std::ofstream(first_path, std::ios::binary | std::ios::trunc)
      << "GNEISS_EDITOR_LAYOUT 2\ninvalid";
  if (layout.initialize(files, "/project-a", visibility).failed() || !pump([&] {
        loaded = layout.advance(visibility);
        return layout.loading();
      }) ||
      loaded != result::invalid_argument || !visibility.console)
    return 10;
  // 目标被目录占据时异步保存失败，不删除目录。
  std::filesystem::remove(first_path);
  std::filesystem::create_directory(first_path);
  if (!draw_workspace_frame() || layout.save(visibility).failed() ||
      !pump([&] { return layout.saving(); }) || layout.save_result().ok() ||
      !std::filesystem::is_directory(first_path))
    return 11;
  // 对象与 ImGui 提前销毁，迟到读取回调仅持有回执。
  {
    editor::editor_layout temporary;
    if (temporary.initialize(files, "/project-b", visibility).failed())
      return 12;
  }
  editor::editor_layout late_save;
  if (late_save.initialize(files, "/project-b", visibility).failed() ||
      late_save.save(visibility).failed())
    return 17;
  ImGui::DestroyContext();
  if (app->request_shutdown().failed())
    return 13;
  result closed;
  if (!pump([&] {
        closed = app->advance_shutdown();
        return closed == result::not_ready;
      }) ||
      closed.failed() || late_save.saving())
    return 14;
  files.reset();
  app.reset();
  std::filesystem::remove_all(root);
  return 0;
} catch (...) {
  return 99;
}
